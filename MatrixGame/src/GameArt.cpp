// MatrixGame - licensed under GPLv2 or any later version.
#include "GameArt.hpp"

#include "GameData.hpp"
#include "CBitmap.hpp"
#include "Pack.hpp"

#include "utils.hpp"

#include <stupid_logger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace GameArt {
namespace {

uint32_t u32(std::string_view bytes, std::size_t at) {
    uint32_t value = 0;
    if (at + 4 <= bytes.size()) std::memcpy(&value, bytes.data() + at, 4);
    return value;
}

// 5-6-5 to 8 bits a channel.
uint32_t rgb(uint16_t v) {
    return ((v >> 11) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | (v & 31) * 255 / 31;
}

// A layer's pixels into `put(x, y, value)` (value: 16 bits for a colour layer, 8 for alpha); false when broken.
template <typename Put>
bool runs(std::string_view bytes, std::size_t offset, std::size_t size, int bpp, Put put) {
    if (size < 16 || offset + size > bytes.size()) return false;
    const int height = int(u32(bytes, offset + 8));
    std::size_t at = offset + 16;
    const std::size_t end = offset + size;
    for (int y = 0; y < height && at < end; ++y) {
        int x = 0;
        while (at < end) {
            const uint8_t code = uint8_t(bytes[at++]);
            if (code == 0) break;
            if (code < 0x80) {
                x += code;
                continue;
            }
            const int count = code & 0x7f;
            if (at + std::size_t(count) * std::size_t(bpp) > end) return false;
            for (int i = 0; i < count; ++i, ++x, at += std::size_t(bpp)) {
                uint16_t value = uint8_t(bytes[at]);
                if (bpp == 2) value |= uint16_t(uint8_t(bytes[at + 1])) << 8;
                put(x, y, value);
            }
        }
    }
    return true;
}

std::filesystem::path g_install;
bool g_install_set = false;

}  // namespace

std::optional<Image> decode_gi(std::string_view bytes) {
    if (bytes.size() < 64 || bytes.substr(0, 4) != std::string_view("gi\0\0", 4)) return std::nullopt;
    const int left = int(u32(bytes, 8)), top = int(u32(bytes, 12)), right = int(u32(bytes, 16)), bottom = int(u32(bytes, 20));
    const uint32_t type = u32(bytes, 40), layers = u32(bytes, 44);
    if (type != 2 || layers < 1 || layers > 3 || right <= left || bottom <= top || right - left > 4096 ||
        bottom - top > 4096 || bytes.size() < 64 + 32 * std::size_t(layers))
        return std::nullopt;
    Image image;
    image.width = right - left;
    image.height = bottom - top;
    image.argb.assign(std::size_t(image.width) * std::size_t(image.height), 0);
    struct Layer {
        std::size_t offset, size;
        int x, y;
    };
    std::array<Layer, 3> layer{};
    for (uint32_t i = 0; i < layers; ++i) {
        const std::size_t at = 64 + 32 * std::size_t(i);
        layer[i] = {u32(bytes, at), u32(bytes, at + 4), int(u32(bytes, at + 8)) - left, int(u32(bytes, at + 12)) - top};
    }
    const auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < image.width && y < image.height; };
    const auto pixel = [&](int x, int y) -> uint32_t & { return image.argb[std::size_t(y) * std::size_t(image.width) + std::size_t(x)]; };
    // The opaque pixels.
    if (!runs(bytes, layer[0].offset, layer[0].size, 2, [&](int x, int y, uint16_t v) {
            x += layer[0].x, y += layer[0].y;
            if (inside(x, y)) pixel(x, y) = 0xff000000u | rgb(v);
        }))
        return std::nullopt;
    if (layers < 3) return image;
    // The half-transparent ones: their colours, then their alpha over what is there.
    std::vector<uint16_t> colour(image.argb.size(), 0);
    std::vector<uint8_t> has(image.argb.size(), 0);
    if (!runs(bytes, layer[1].offset, layer[1].size, 2, [&](int x, int y, uint16_t v) {
            x += layer[1].x, y += layer[1].y;
            if (!inside(x, y)) return;
            colour[std::size_t(y) * std::size_t(image.width) + std::size_t(x)] = v;
            has[std::size_t(y) * std::size_t(image.width) + std::size_t(x)] = 1;
        }))
        return std::nullopt;
    if (!runs(bytes, layer[2].offset, layer[2].size, 1, [&](int x, int y, uint16_t v) {
            x += layer[2].x, y += layer[2].y;
            if (!inside(x, y)) return;
            const std::size_t i = std::size_t(y) * std::size_t(image.width) + std::size_t(x);
            if (!has[i]) return;
            const uint32_t a = std::min<uint32_t>(v, 63) * 255 / 63, c = rgb(colour[i]), under = image.argb[i];
            const uint32_t under_a = under >> 24;
            // Over the opaque pixel, if any: the result stays opaque.
            const uint32_t out_a = a + under_a * (255 - a) / 255;
            uint32_t result = out_a << 24;
            for (int shift : {16, 8, 0}) {
                const uint32_t top_c = (c >> shift) & 255, low_c = (under >> shift) & 255;
                const uint32_t mixed = out_a ? (top_c * a + low_c * under_a * (255 - a) / 255) / out_a : 0;
                result |= std::min<uint32_t>(mixed, 255) << shift;
            }
            image.argb[i] = result;
        }))
        return std::nullopt;
    return image;
}

std::optional<std::string> pkg_file(const std::filesystem::path &pkg, const std::string &name) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(pkg, error)) return std::nullopt;
    Base::CPackFile pack(nullptr, pkg.wstring().c_str());
    if (!pack.OpenPacketFile()) return std::nullopt;
    std::optional<std::string> result;
    const DWORD handle = pack.Open(name, GENERIC_READ);
    if (handle != 0xffffffff) {
        std::string bytes(pack.GetSize(handle), '\0');
        if (pack.Read(handle, bytes.data(), int(bytes.size()))) result = std::move(bytes);
        pack.Close(handle);
    }
    pack.ClosePacketFile();
    return result;
}

void set_install(const std::filesystem::path &game) {
    g_install = game;
    g_install_set = true;
}

const Image *button(Button which) {
    static const std::array<std::optional<Image>, 3> buttons = [] {
        std::array<std::optional<Image>, 3> read{};
        std::filesystem::path game = g_install;
        if (!g_install_set) {
            std::error_code error;
            const auto install = GameData::load_install(std::filesystem::current_path(error));
            if (install) game = install->path;
        }
        if (game.empty()) return read;
        const auto load = [&](const wchar_t *pkg, const char *name) -> std::optional<Image> {
            const auto bytes = pkg_file(game / L"DATA" / pkg, name);
            auto image = bytes ? decode_gi(*bytes) : std::nullopt;
            if (!image) lgr.warning("Game art: no {} in {}")(name, utils::from_wstring((game / L"DATA" / pkg).wstring()));
            return image;
        };
        read[0] = load(L"forms.pkg", "DATA\\FormOptions2\\2SwitchN.gi");
        read[1] = load(L"forms.pkg", "DATA\\FormOptions2\\2SwitchD.gi");
        read[2] = load(L"common.pkg", "DATA\\Msg\\2UserN.gi");
        return read;
    }();
    const auto &image = buttons[std::size_t(which)];
    return image ? &*image : nullptr;
}

Image resized(const Image &image, int width, int height) {
    Image out;
    out.width = std::max(1, width);
    out.height = std::max(1, height);
    out.argb.assign(std::size_t(out.width) * std::size_t(out.height), 0);
    if (image.width <= 0 || image.height <= 0) return out;
    const auto at = [&](int x, int y) {
        x = std::clamp(x, 0, image.width - 1), y = std::clamp(y, 0, image.height - 1);
        return image.argb[std::size_t(y) * std::size_t(image.width) + std::size_t(x)];
    };
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const float sx = (x + 0.5f) * image.width / out.width - 0.5f, sy = (y + 0.5f) * image.height / out.height - 0.5f;
            const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
            const float fx = sx - x0, fy = sy - y0;
            float sum[4] = {};  // premultiplied b, g, r and a
            const std::pair<uint32_t, float> taps[] = {{at(x0, y0), (1 - fx) * (1 - fy)},
                                                        {at(x0 + 1, y0), fx * (1 - fy)},
                                                        {at(x0, y0 + 1), (1 - fx) * fy},
                                                        {at(x0 + 1, y0 + 1), fx * fy}};
            for (const auto &[p, w] : taps) {
                const float a = float(p >> 24) / 255.0f;
                for (int c = 0; c < 3; ++c) sum[c] += float((p >> (8 * c)) & 255) * a * w;
                sum[3] += a * w;
            }
            uint32_t result = uint32_t(std::lround(std::clamp(sum[3], 0.0f, 1.0f) * 255)) << 24;
            for (int c = 0; c < 3; ++c)
                result |= uint32_t(std::lround(std::clamp(sum[3] > 0 ? sum[c] / sum[3] : 0.0f, 0.0f, 255.0f))) << (8 * c);
            out.argb[std::size_t(y) * std::size_t(out.width) + std::size_t(x)] = result;
        }
    return out;
}

namespace {

// A round button: a dark rim, inside lit blue (on) or dark (off), a sphere's shading, smoothed edges.
Image own_switch(int size, bool on) {
    Image out;
    out.width = out.height = std::max(1, size);
    out.argb.resize(std::size_t(size) * std::size_t(size));
    const float r = size * 0.5f, rim = std::max(1.5f, size * 0.12f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = x + 0.5f - r, dy = y + 0.5f - r, d = std::sqrt(dx * dx + dy * dy);
            const float alpha = std::clamp(r - d, 0.0f, 1.0f);  // the edge's coverage
            // The rim: metal, lighter at the top; inside: the light, brightest a little above the centre.
            float red, green, blue;
            if (d > r - rim) {
                const float k = 0.55f - 0.25f * dy / r;
                red = 70 * k + 30, green = 80 * k + 34, blue = 90 * k + 38;
            }
            else {
                const float hx = dx + r * 0.2f, hy = dy + r * 0.3f;
                const float t = std::clamp(1.0f - std::sqrt(hx * hx + hy * hy) / (r - rim), 0.0f, 1.0f);
                if (on) red = 20 + 200 * t * t, green = 110 + 140 * t, blue = 200 + 55 * t;
                else red = 38 + 40 * t * t, green = 42 + 44 * t * t, blue = 48 + 48 * t * t;
            }
            out.argb[std::size_t(y) * std::size_t(size) + std::size_t(x)] =
                    (uint32_t(alpha * 255) << 24) | (uint32_t(std::min(red, 255.0f)) << 16) |
                    (uint32_t(std::min(green, 255.0f)) << 8) | uint32_t(std::min(blue, 255.0f));
        }
    return out;
}

// The lit button with a white "i".
Image own_info(int size) {
    Image out = own_switch(size, true);
    const float r = size * 0.5f, stroke = std::max(1.0f, size * 0.07f);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const float dx = x + 0.5f - r, py = (y + 0.5f) / size;
            const float dot = std::hypot(dx, (py - 0.3f) * size) - size * 0.08f;
            const float bar = std::max(std::abs(dx) - stroke, std::max(0.45f - py, py - 0.76f) * size);
            const float cover = std::clamp(0.5f - std::min(dot, bar), 0.0f, 1.0f);
            uint32_t &pixel = out.argb[std::size_t(y) * std::size_t(out.width) + std::size_t(x)];
            uint32_t mixed = pixel & 0xff000000u;
            for (int shift : {16, 8, 0}) {
                const float c = float((pixel >> shift) & 255);
                mixed |= uint32_t(c + (255 - c) * cover) << shift;
            }
            pixel = mixed;
        }
    return out;
}

}  // namespace

Image picture(Button which, int size) {
    if (const Image *image = button(which)) return resized(*image, size, size);
    return which == Button::Info ? own_info(size) : own_switch(size, which == Button::SwitchOn);
}

std::unique_ptr<CBitmap> bitmap(const Image &image) {
    auto out = std::make_unique<CBitmap>();
    out->CreateRGBA(image.width, image.height);
    for (int y = 0; y < image.height; ++y)
        std::memcpy(out->Data() + y * out->Pitch(), image.argb.data() + std::size_t(y) * std::size_t(image.width),
                    std::size_t(image.width) * 4);
    return out;
}

}  // namespace GameArt
