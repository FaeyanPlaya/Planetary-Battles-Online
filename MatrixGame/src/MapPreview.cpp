// MatrixGame - licensed under GPLv2 or any later version.
#include "MapPreview.hpp"
#include "MapModel.hpp"
#include "MatrixGamePathUtils.hpp"
#include "Common.hpp"
#include "StringConstants.hpp"
#include "MatrixMap.hpp"
#include "CBlockPar.hpp"
#include "CFile.hpp"

#include "Interface/MatrixHint.hpp"
#include "3g.hpp"

#include "CBitmap.hpp"
#include "CBuf.hpp"
#include "CException.hpp"
#include "CStorage.hpp"
#include "utils.hpp"

#include <d3dx9.h>

#include <stupid_logger.hpp>

#include <cstring>
#include <filesystem>
#include <format>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace MapPreview {
namespace {

std::unique_ptr<CBitmap> preview;
SettingsModel::Resolution dimensions{0, 0};
std::wstring shown_path;  // the last path attempted
SettingsModel::Resolution shown_box{0, 0};  // and the box it was scaled into

// Decodes a JPEG or PNG preview, scaled into the preview box, into an opaque ARGB bitmap.
bool decode(const std::wstring &path) {
    CBuf data;
    data.LoadFromFile(path);
    D3DXIMAGE_INFO info{};
    if (FAILED(D3DXGetImageInfoFromFileInMemory(data.Get(), static_cast<UINT>(data.Len()), &info))) return false;
    const auto size = MapModel::preview_size(static_cast<int>(info.Width), static_cast<int>(info.Height), shown_box);
    if (size.width <= 0) return false;
    IDirect3DSurface9 *surface = nullptr;
    if (FAILED(g_D3DD->CreateOffscreenPlainSurface(size.width, size.height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH,
                                                   &surface, nullptr)))
        return false;
    bool decoded = false;
    if (SUCCEEDED(D3DXLoadSurfaceFromFileInMemory(surface, nullptr, nullptr, data.Get(), static_cast<UINT>(data.Len()),
                                                  nullptr, D3DX_FILTER_TRIANGLE, 0, nullptr))) {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            preview = std::make_unique<CBitmap>();
            preview->CreateRGBA(size.width, size.height);
            for (int y = 0; y < size.height; ++y) {
                auto *target = preview->Data() + y * preview->Pitch();
                std::memcpy(target, static_cast<const uint8_t *>(locked.pBits) + y * locked.Pitch, size.width * 4);
                for (int x = 0; x < size.width; ++x) target[x * 4 + 3] = 0xff;
            }
            surface->UnlockRect();
            dimensions = size;
            decoded = true;
        }
    }
    surface->Release();
    return decoded;
}

std::map<std::wstring, std::wstring> cached_minimaps;  // by map path, found or not

// What the map's own previews show over the ground: the battle minimap's icons of its bases,
// factories, turrets and robots.
struct Mark {
    float u, v;  // 0..1 over the minimap picture
    int side;
    enum Kind { Robot, Turret, Factory, Base } kind;
};
std::map<std::wstring, std::vector<Mark>> marks_of;  // by cached minimap file

// The minimap picture covers a square of the map's larger side around its centre (CMinimap::World2Map).
std::vector<Mark> read_marks(Base::CStorage &storage) {
    std::vector<Mark> marks;
    auto *names = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, Base::ST_WCHAR);
    auto *values = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, Base::ST_WCHAR);
    if (!names || !values) return marks;
    const int ix = names->FindAsWStr(DATA_SIZEX), iy = names->FindAsWStr(DATA_SIZEY);
    if (ix < 0 || iy < 0) return marks;
    const float sx = float(values->GetAsParamParser(ix).GetInt()) * GLOBAL_SCALE;
    const float sy = float(values->GetAsParamParser(iy).GetInt()) * GLOBAL_SCALE;
    const float square = std::max(sx, sy);
    if (square <= 0) return marks;
    const auto add = [&](const wchar *table, const wchar *x, const wchar *y, const wchar *side, const wchar *kind,
                         Mark::Kind as) {
        auto *bx = storage.GetBuf(table, x, Base::ST_FLOAT);
        auto *by = storage.GetBuf(table, y, Base::ST_FLOAT);
        auto *bs = storage.GetBuf(table, side, Base::ST_BYTE);
        auto *bk = kind ? storage.GetBuf(table, kind, Base::ST_BYTE) : nullptr;
        if (!bx || !by || !bs) return;
        const int n = std::min({bx->GetArrayLength(0), by->GetArrayLength(0), bs->GetArrayLength(0)});
        for (int i = 0; i < n; ++i) {
            const int s = bs->GetFirst<BYTE>(0)[i];
            if (s > 4) continue;  // ruins
            Mark m{0.5f + (bx->GetFirst<float>(0)[i] - sx * 0.5f) / square,
                   0.5f + (by->GetFirst<float>(0)[i] - sy * 0.5f) / square, s, as};
            if (bk) m.kind = bk->GetFirst<BYTE>(0)[i] == 0 ? Mark::Base : Mark::Factory;
            marks.push_back(m);
        }
    };
    add(DATA_ROBOTS, DATA_ROBOTS_X, DATA_ROBOTS_Y, DATA_ROBOTS_SIDE, nullptr, Mark::Robot);
    add(DATA_CANNONS, DATA_CANNONS_X, DATA_CANNONS_Y, DATA_CANNONS_SIDE, nullptr, Mark::Turret);
    add(DATA_BUILDINGS, DATA_BUILDINGS_X, DATA_BUILDINGS_Y, DATA_BUILDINGS_SIDE, DATA_BUILDINGS_KIND, Mark::Factory);
    // Drawn in this order, as the battle minimap draws them: buildings over what stands at them.
    std::stable_sort(marks.begin(), marks.end(), [](const Mark &a, const Mark &b) { return a.kind < b.kind; });
    return marks;
}

// The side's minimap colour (CFG Side block: "Yellow,227,158,31,Minimap,255,255,0"; 0 the neutral grey).
DWORD side_colour(int side) {
    if (CBlockPar *block = g_MatrixData ? g_MatrixData->BlockGetNE(L"Side") : nullptr) {
        const auto value = block->ParGetNE(utils::format(L"%d", side));
        if (ParamParser{value}.GetCountPar(L",") >= 8)
            return 0xFF000000 | (DWORD(ParamParser{value}.GetStrPar(5, L",").GetInt() & 255) << 16) |
                   (DWORD(ParamParser{value}.GetStrPar(6, L",").GetInt() & 255) << 8) |
                   DWORD(ParamParser{value}.GetStrPar(7, L",").GetInt() & 255);
    }
    return 0xFF808080;
}

// The battle minimap's icons (CFG Minimap block: "Matrix\Textures\Minimap\radarIcons,48,0,16,16"), ARGB.
struct Icons {
    std::vector<std::uint32_t> pixels;
    int width = 0, height = 0;
    struct Cell { int x = 0, y = 0, w = 0, h = 0; } cells[4];
    bool loaded = false, tried = false;
};
Icons icons;

bool load_icons() {
    if (icons.tried) return icons.loaded;
    icons.tried = true;
    CBlockPar *block = g_MatrixData ? g_MatrixData->BlockGetNE(PAR_SOURCE_MINIMAP) : nullptr;
    if (!block) return false;
    const wchar *names[4] = {PAR_SOURCE_MINIMAP_ROBOT, PAR_SOURCE_MINIMAP_TURRET, PAR_SOURCE_MINIMAP_FACTORY,
                             PAR_SOURCE_MINIMAP_BASE};
    std::wstring texture;
    for (int i = 0; i < 4; ++i) {
        const auto value = block->ParGetNE(names[i]);
        if (ParamParser{value}.GetCountPar(L",") < 5) return false;
        texture = ParamParser{value}.GetStrPar(0, L",");
        icons.cells[i] = {ParamParser{value}.GetStrPar(1, L",").GetInt(), ParamParser{value}.GetStrPar(2, L",").GetInt(),
                          ParamParser{value}.GetStrPar(3, L",").GetInt(), ParamParser{value}.GetStrPar(4, L",").GetInt()};
    }
    std::wstring file;
    if (!CFile::FileExist(file, texture.c_str(), L"dds~png")) return false;
    CBuf data;
    data.LoadFromFile(file);
    D3DXIMAGE_INFO info{};
    if (FAILED(D3DXGetImageInfoFromFileInMemory(data.Get(), static_cast<UINT>(data.Len()), &info))) return false;
    IDirect3DSurface9 *surface = nullptr;
    if (FAILED(g_D3DD->CreateOffscreenPlainSurface(info.Width, info.Height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surface,
                                                   nullptr)))
        return false;
    if (SUCCEEDED(D3DXLoadSurfaceFromFileInMemory(surface, nullptr, nullptr, data.Get(), static_cast<UINT>(data.Len()),
                                                  nullptr, D3DX_FILTER_NONE, 0, nullptr))) {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            icons.width = int(info.Width);
            icons.height = int(info.Height);
            icons.pixels.resize(std::size_t(icons.width) * std::size_t(icons.height));
            for (int y = 0; y < icons.height; ++y)
                std::memcpy(&icons.pixels[std::size_t(y) * std::size_t(icons.width)],
                            static_cast<const uint8_t *>(locked.pBits) + y * locked.Pitch, std::size_t(icons.width) * 4);
            surface->UnlockRect();
            icons.loaded = true;
        }
    }
    surface->Release();
    return icons.loaded;
}

// One icon centred at (cx, cy), `size` pixels across: the icon's texels averaged into each pixel,
// tinted by the side's colour (the battle minimap modulates the texture by it) and blended by alpha.
void draw_icon(CBitmap &bmp, float cx, float cy, const Icons::Cell &cell, int size, DWORD colour) {
    const int x0 = int(std::floor(cx - float(size) * 0.5f)), y0 = int(std::floor(cy - float(size) * 0.5f));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int tx = x0 + x, ty = y0 + y;
            if (tx < 0 || ty < 0 || tx >= bmp.SizeX() || ty >= bmp.SizeY()) continue;
            // The texels under this pixel.
            const int sx0 = cell.x + x * cell.w / size, sx1 = std::max(sx0 + 1, cell.x + (x + 1) * cell.w / size);
            const int sy0 = cell.y + y * cell.h / size, sy1 = std::max(sy0 + 1, cell.y + (y + 1) * cell.h / size);
            float a = 0, r = 0, g = 0, b = 0;
            int n = 0;
            for (int sy = sy0; sy < sy1 && sy < icons.height; ++sy)
                for (int sx = sx0; sx < sx1 && sx < icons.width; ++sx, ++n) {
                    const std::uint32_t t = icons.pixels[std::size_t(sy) * std::size_t(icons.width) + std::size_t(sx)];
                    const float ta = float(t >> 24) / 255.0f;
                    a += ta;
                    r += ta * float((t >> 16) & 255);
                    g += ta * float((t >> 8) & 255);
                    b += ta * float(t & 255);
                }
            if (n == 0 || a <= 0) continue;
            r /= a, g /= a, b /= a, a /= float(n);
            r *= float((colour >> 16) & 255) / 255.0f;
            g *= float((colour >> 8) & 255) / 255.0f;
            b *= float(colour & 255) / 255.0f;
            uint8_t *px = bmp.Data() + ty * bmp.Pitch() + tx * 4;
            px[0] = uint8_t(float(px[0]) * (1 - a) + b * a);
            px[1] = uint8_t(float(px[1]) * (1 - a) + g * a);
            px[2] = uint8_t(float(px[2]) * (1 - a) + r * a);
        }
}

void draw_marks(CBitmap &bmp, const std::vector<Mark> &marks) {
    if (!load_icons()) return;
    // As large as on the maps' own previews (145 pixels across, icons of about 13).
    const int size = std::max(5, int(std::lround(13.0f * float(bmp.SizeX()) / 145.0f)));
    for (const Mark &m : marks)
        draw_icon(bmp, m.u * float(bmp.SizeX()), m.v * float(bmp.SizeY()), icons.cells[m.kind], size, side_colour(m.side));
}

std::wstring cached_minimap(const std::wstring &map_path) {
    if (const auto known = cached_minimaps.find(map_path); known != cached_minimaps.end()) return known->second;
    std::wstring found;
    try {
        Base::CStorage storage;
        if (storage.Load(map_path.c_str())) {
            auto *names = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_NAME, Base::ST_WCHAR);
            auto *values = storage.GetBuf(DATA_PROPERTIES, DATA_PROPERTIES_VALUE, Base::ST_WCHAR);
            const int at = names && values ? names->FindAsWStr(DATA_UNIQID) : -1;
            const DWORD uniq = at >= 0 ? values->GetAsParamParser(at).GetDword() : 0;
            // The name the minimap cache uses (CMinimap::RenderBackground).
            const std::wstring file = std::format(L"{}\\{}.{:X}",PathToOutputFiles(FOLDER_NAME_CACHE),
                                                  std::filesystem::path(map_path).filename().wstring(), uniq);
            std::error_code error;
            if (at >= 0 && std::filesystem::is_regular_file(file, error)) {
                found = file;
                marks_of[file] = read_marks(storage);
            }
        }
    }
    catch (...) {
    }
    cached_minimaps[map_path] = found;
    return found;
}

}  // namespace

std::wstring source(const MapCatalog::Map &map) { return !map.preview.empty() ? map.preview : cached_minimap(map.path); }

void show(const std::wstring &path) { show(path, {MapModel::Layout::preview_width, MapModel::Layout::preview_height}); }

void show(const std::wstring &path, SettingsModel::Resolution box) {
    if (path == shown_path && box.width == shown_box.width && box.height == shown_box.height) return;
    release();
    shown_path = path;
    shown_box = box;
    if (path.empty()) return;
    try {
        if (decode(path)) {
            if (const auto marks = marks_of.find(path); marks != marks_of.end()) draw_marks(*preview, marks->second);
            CMatrixHint::SetRuntimeBitmap(MapModel::preview_bitmap, preview.get());
            return;
        }
    }
    catch (const CException &) {
    }
    catch (const std::exception &) {
    }
    lgr.warning("Cannot show the map preview {}")(utils::from_wstring(path));
}

SettingsModel::Resolution size() { return dimensions; }

void release() {
    CMatrixHint::SetRuntimeBitmap(MapModel::preview_bitmap, nullptr);
    preview.reset();
    dimensions = {0, 0};
    shown_path.clear();
}

}  // namespace MapPreview
