// MatrixGame - licensed under GPLv2 or any later version.
#include "ShaderPacks.hpp"

#include "utils.hpp"

#include <windows.h>
#include <d3d9.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

namespace ShaderPacks {
namespace {

std::wstring lower(std::wstring text) {
    for (auto &ch : text)
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
    return text;
}

std::vector<std::wstring> split(std::wstring_view list, wchar_t separator) {
    std::vector<std::wstring> items;
    while (true) {
        const auto at = list.find(separator);
        items.push_back(utils::trim(std::wstring(list.substr(0, at))));
        if (at == std::wstring_view::npos) break;
        list = list.substr(at + 1);
    }
    return items;
}

// A name the code can use: a letter or _, then letters, digits, _ (and not one the engine's prelude has).
bool code_name(const std::string &name) {
    if (name.empty() || name.size() > 32) return false;
    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_')) return false;
    for (char ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'))
            return false;
    // (the prelude's own: a picture or a setting of such a name would not compile, or would change the engine's)
    static const char *const taken[] = {
            "SceneTex",     "DepthTex",     "PrevTex",     "SolidTex",     "screen",       "main",        "PBO",
            "On",           "Off",          "ScreenSize",  "SceneSize",    "Time",         "CameraPos",   "SunDir",
            "SunColor",     "AmbientColor", "FogColor",    "FogRange",     "DepthParams",  "State",       "MapInfo",
            "View",         "Proj",         "ViewProj",    "InvViewProj",  "PrevViewProj", "LightCount",  "LightPos",
            "LightColor",   "LightInfo",    "ShadowColor", "SkyHorizon",   "MAX_LIGHTS",   "LIGHT_GLOW",  "LIGHT_FIRE",
            "LIGHT_FLASH",  "LIGHT_SHOT",   "ViewDepth",   "SceneDepth",   "WorldPos",     "Luma",        "ScreenPos",
            "ScenePixel",   "SceneNormal",  "SceneHeight", "ReliefSlope",  "GeneratedNormal", "PrevCameraPos",
            "ShadowTex",    "ShadowViewProj", "ShadowInfo", "ShadowPos",   "ShadowDepth",  "ShadowLit",
            "World",        "WorldTex",     "WorldInfo",   "PBO_WORLD",    "MAT_LAND",     "MAT_OBJECT",
            "MAT_ROBOT",    "MAT_BUILDING", "MAT_CANNON",  "MAT_FLYER",    "pbo_world",    "LandInfo",
            "VoxelTex",     "VoxelAt",      "VoxelOrigin", "VoxelScale",   "VoxelExtent",
            "Water",        "WaterTex",     "ReflTex",     "UnderTex",     "PBO_WATER",    "MAT_WATER",   "pbo_water",
            "Effect",       "PBO_EFFECT",   "pbo_effect",  "EffectInfo",   "Faded",        "PBO_TFactor",
            "PROFILE"};
    return std::ranges::none_of(taken, [&](const char *t) { return name == t; });
}

std::string ascii(const std::wstring &text) {
    std::string result;
    for (wchar_t ch : text) {
        if (ch >= 0x80) return {};
        result += char(ch);
    }
    return result;
}

// A path inside a pack: '/' between folders, lower case, no "..", no drive or root. Empty: not one.
std::wstring inside(std::wstring_view name) {
    std::wstring path(name);
    std::replace(path.begin(), path.end(), L'\\', L'/');
    while (path.starts_with(L"./")) path.erase(0, 2);
    if (path.empty() || path.starts_with(L"/") || path.find(L':') != std::wstring::npos) return {};
    for (const auto &part : split(path, L'/'))
        if (part.empty() || part == L"." || part == L"..") return {};
    return lower(path);
}

std::uint32_t u16(std::string_view bytes, std::size_t at) {
    return std::uint8_t(bytes[at]) | (std::uint8_t(bytes[at + 1]) << 8);
}
std::uint32_t u32(std::string_view bytes, std::size_t at) {
    return u16(bytes, at) | (u16(bytes, at + 2) << 16);
}

std::optional<std::string> inflate_raw(std::string_view packed, std::size_t size) {
    std::string out(size, '\0');
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return std::nullopt;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed.data()));
    stream.avail_in = uInt(packed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool whole = result == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    if (!whole) return std::nullopt;
    return out;
}

std::optional<std::string> read_file(const std::filesystem::path &file, std::size_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > limit) return std::nullopt;
    std::ifstream stream(file, std::ios::binary);
    std::string bytes(std::size_t(size), '\0');
    if (!stream.read(bytes.data(), std::streamsize(size))) return std::nullopt;
    return bytes;
}

// The files under shaders.txt's folder: its folder becomes the top (the only folder of a zip may hold it).
std::optional<Files> rooted(Files files, std::string &error) {
    if (files.contains(description_file)) return files;
    std::wstring top;
    for (const auto &[name, bytes] : files) {
        const auto slash = name.find(L'/');
        const std::wstring first = slash == std::wstring::npos ? std::wstring() : name.substr(0, slash + 1);
        if (first.empty() || (!top.empty() && first != top)) {
            error = "no shaders.txt at its top";
            return std::nullopt;
        }
        top = first;
    }
    if (top.empty() || !files.contains(top + description_file)) {
        error = "no shaders.txt at its top";
        return std::nullopt;
    }
    Files result;
    for (auto &[name, bytes] : files) result.emplace(name.substr(top.size()), std::move(bytes));
    return result;
}

std::optional<Mods::SettingLine> setting(const std::wstring &key, const std::wstring &value) {
    auto line = Mods::parse_setting(key, value);
    if (line && !code_name(line->id)) return std::nullopt;
    return line;
}

std::optional<Format> format_of(const std::wstring &name) {
    const std::wstring text = lower(name);
    if (text.empty() || text == L"rgba8") return Format::RGBA8;
    if (text == L"rgba16f") return Format::RGBA16F;
    if (text == L"rg16f") return Format::RG16F;
    if (text == L"r32f") return Format::R32F;
    return std::nullopt;
}

bool image_file(const std::wstring &file) {
    for (const wchar_t *ext : {L".png", L".dds", L".bmp", L".tga", L".jpg"})
        if (file.ends_with(ext)) return true;
    return false;
}

std::wstring text(const char *ascii_text) { return std::wstring(ascii_text, ascii_text + std::strlen(ascii_text)); }

std::vector<std::wstring> words(std::wstring_view list) {
    std::vector<std::wstring> result;
    std::wstring word;
    for (wchar_t ch : list) {
        if (ch == L' ' || ch == L'\t') {
            if (!word.empty()) result.push_back(std::move(word));
            word.clear();
        }
        else word += ch;
    }
    if (!word.empty()) result.push_back(std::move(word));
    return result;
}

// A screen's rows by their names (Screen=, Screen.<Id>=); a name that is nothing of the pack's is a problem.
void resolve(Pack &pack, std::size_t screen, const std::wstring &rows, std::vector<std::wstring> &problems) {
    for (const auto &word : words(rows)) {
        Item item;
        if (word == L"-") {
            pack.screens[screen].items.push_back(item);
            continue;
        }
        if (ascii(word) == profile_row) {
            if (pack.profiles.empty()) problems.push_back(L"Screen row " + word + L" (no Profile)");
            else pack.screens[screen].items.push_back({Item::Kind::Profile, -1});
            continue;
        }
        if (word.size() > 2 && word.front() == L'[' && word.back() == L']') {
            const std::string id = ascii(word.substr(1, word.size() - 2));
            const auto found = std::ranges::find(pack.screens, id, &Screen::id);
            if (id.empty() || found == pack.screens.end()) {
                problems.push_back(L"Screen " + word);
                continue;
            }
            item = {Item::Kind::Screen, int(found - pack.screens.begin())};
        }
        else if (const auto setting = std::ranges::find(pack.settings, ascii(word), &Mods::SettingLine::id); setting != pack.settings.end())
            item = {Item::Kind::Setting, int(setting - pack.settings.begin())};
        else if (const auto note = std::ranges::find(pack.notes, ascii(word), &Note::id); note != pack.notes.end())
            item = {Item::Kind::Note, int(note - pack.notes.begin())};
        else {
            problems.push_back(L"Screen row " + word);
            continue;
        }
        pack.screens[screen].items.push_back(item);
    }
}

std::optional<Choice> g_choice;
std::uint32_t g_version = 1;

// The names in a file's code and in what it includes ("#include" of the pack's files, one in another up to 16 deep).
void names_of(const Files &files, const std::wstring &name, std::set<std::string> &names, std::set<std::wstring> &seen,
              int depth) {
    const std::wstring path = inside(name);
    if (path.empty() || depth > 16 || !seen.insert(path).second) return;
    const auto found = files.find(path);
    if (found == files.end()) return;
    const std::string &code = found->second;
    const auto letter = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; };
    for (std::size_t at = 0; at < code.size();) {
        if (!letter(code[at])) {
            ++at;
            continue;
        }
        std::size_t end = at;
        while (end < code.size() && (letter(code[end]) || (code[end] >= '0' && code[end] <= '9'))) ++end;
        const std::string word = code.substr(at, end - at);
        at = end;
        if (word != "include") {
            names.insert(word);
            continue;
        }
        // #include "file" or <file>, on its line.
        const std::size_t line_end = std::min(code.find('\n', at), code.size());
        const std::size_t open = code.find_first_of("\"<", at);
        if (open >= line_end) continue;
        const std::size_t close = code.find_first_of("\">", open + 1);
        if (close >= line_end) continue;
        std::wstring included;
        try {
            included = utils::to_wstring(code.substr(open + 1, close - open - 1));
        }
        catch (...) {
            continue;
        }
        names_of(files, included, names, seen, depth + 1);
        at = close + 1;
    }
}

}  // namespace

Inputs pass_inputs(const Pack &pack, const Files &files, const Pass &pass) {
    std::set<std::string> names;
    std::set<std::wstring> seen;
    names_of(files, pass.file, names, seen, 0);
    Inputs inputs;
    for (int i = 0; i < int(pack.targets.size()); ++i)
        if (names.contains(pack.targets[i].name)) inputs.targets.push_back(i);
    for (int i = 0; i < int(pack.textures.size()); ++i)
        if (names.contains(pack.textures[i].name)) inputs.textures.push_back(i);
    inputs.shadow = pack.shadow.size > 0 &&
                    (names.contains("ShadowTex") || names.contains("ShadowDepth") || names.contains("ShadowLit"));
    inputs.voxels = pack.voxels.size > 0 && (names.contains("VoxelTex") || names.contains("VoxelAt"));
    return inputs;
}

Inputs all_inputs(const Pack &pack) {
    Inputs inputs;
    for (int i = 0; i < int(pack.targets.size()); ++i) inputs.targets.push_back(i);
    for (int i = 0; i < int(pack.textures.size()); ++i) inputs.textures.push_back(i);
    inputs.shadow = pack.shadow.size > 0;
    inputs.voxels = pack.voxels.size > 0;
    return inputs;
}

Pack parse(std::wstring_view text_view, std::wstring id) {
    Pack pack;
    pack.id = std::move(id);
    bool described = false, described_en = false;
    std::vector<std::wstring> problems;
    std::optional<std::wstring> first_rows;  // Screen=
    std::vector<std::wstring> screen_rows;   // Screen.<Id>='s, as pack.screens after the first
    pack.screens.emplace_back();
    for (const auto &raw : split(text_view, L'\n')) {
        const std::wstring line = utils::trim(raw);
        if (line.empty() || line.starts_with(L"#") || line.starts_with(L"//")) continue;
        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key_as_written = utils::trim(line.substr(0, eq));
        const std::wstring key = lower(key_as_written);
        const std::wstring value = utils::trim(line.substr(eq + 1));
        if (key == L"name") pack.name = value;
        else if (key == L"nameen") pack.name_en = value;
        else if (key == L"description") pack.description += (described ? L"\n" : L"") + value, described = true;
        else if (key == L"descriptionen") pack.description_en += (described_en ? L"\n" : L"") + value, described_en = true;
        else if (key == L"author") pack.author = value;
        else if (key.starts_with(L"setting.")) {
            if (auto parsed = setting(key_as_written, value)) pack.settings.push_back(std::move(*parsed));
            else problems.push_back(L"Setting " + key_as_written.substr(8));
        }
        else if (key.starts_with(L"target.")) {
            auto parts = split(value, L'|');
            parts.resize(2);
            Target target;
            target.name = ascii(key_as_written.substr(7));
            const auto format = format_of(parts[1]);
            float scale = 0;
            int width = 0, height = 0;
            const auto by = lower(parts[0]).find(L'x');
            try {
                if (by != std::wstring::npos) {
                    std::size_t used_w = 0, used_h = 0;
                    const std::wstring w = parts[0].substr(0, by), h = parts[0].substr(by + 1);
                    width = std::stoi(w, &used_w);
                    height = std::stoi(h, &used_h);
                    if (used_w != w.size() || used_h != h.size()) width = height = 0;
                    scale = 1.0f;
                }
                else
                    scale = parts[0].empty() ? 1.0f : std::stof(parts[0]);
            }
            catch (...) {
            }
            const bool sized = by == std::wstring::npos || (width >= 1 && width <= 4096 && height >= 1 && height <= 4096);
            if (!code_name(target.name) || !format || !sized || !(scale >= 1.0f / 16 && scale <= 2.0f)) {
                problems.push_back(L"Target " + key_as_written.substr(7));
                continue;
            }
            target.scale = scale;
            target.width = width;
            target.height = height;
            target.format = *format;
            pack.targets.push_back(std::move(target));
        }
        else if (key.starts_with(L"texture.")) {
            auto parts = split(value, L'|');
            parts.resize(3);
            Texture texture;
            texture.name = ascii(key_as_written.substr(8));
            texture.file = inside(parts[0]);
            texture.clamp = lower(parts[1]) == L"clamp";
            texture.point = lower(parts[2]) == L"point";
            if (!code_name(texture.name) || texture.file.empty() || !image_file(texture.file)) {
                problems.push_back(L"Texture " + key_as_written.substr(8));
                continue;
            }
            pack.textures.push_back(std::move(texture));
        }
        else if (key == L"pass") {
            auto parts = split(value, L'|');
            parts.resize(3);
            Pass pass;
            pass.file = inside(parts[0]);
            pass.output = ascii(parts[1]);
            if (!parts[2].empty()) pass.entry = ascii(parts[2]);
            if (pass.file.empty() || pass.output.empty() || pass.entry.empty()) {
                problems.push_back(L"Pass " + value);
                continue;
            }
            pack.passes.push_back(std::move(pass));
        }
        else if (key == L"screen") first_rows = value;
        else if (key.starts_with(L"screen.")) {
            auto parts = split(value, L'|');
            parts.resize(3);
            Screen screen{ascii(key_as_written.substr(7)), parts[0], parts[1], {}};
            if (!code_name(screen.id) || (screen.russian.empty() && screen.english.empty())) {
                problems.push_back(L"Screen " + key_as_written.substr(7));
                continue;
            }
            pack.screens.push_back(std::move(screen));
            screen_rows.push_back(parts[2]);
        }
        else if (key.starts_with(L"note.")) {
            auto parts = split(value, L'|');
            parts.resize(2);
            Note note{ascii(key_as_written.substr(5)), parts[0], parts[1]};
            if (!code_name(note.id) || (note.russian.empty() && note.english.empty())) {
                problems.push_back(L"Note " + key_as_written.substr(5));
                continue;
            }
            pack.notes.push_back(std::move(note));
        }
        else if (key.starts_with(L"game.")) {
            const std::string name = ascii(key_as_written.substr(5));
            const std::wstring said = lower(value);
            const auto known = std::ranges::find_if(game_settings, [&](const char *known_name) {
                return lower(utils::to_wstring(known_name)) == lower(utils::to_wstring(name));
            });
            if (known == std::end(game_settings) || (said != L"on" && said != L"off")) {
                problems.push_back(L"Game " + key_as_written.substr(5));
                continue;
            }
            pack.game[*known] = said == L"on";
        }
        else if (key == L"voxels") {
            auto parts = split(value, L'|');
            parts.resize(2);
            Voxels voxels;
            try {
                std::size_t used = 0;
                voxels.size = std::stof(parts[0], &used);
                if (used != parts[0].size()) voxels.size = 0;
            }
            catch (...) {
                voxels.size = 0;
            }
            voxels.when = ascii(parts[1]);
            if (!(voxels.size >= 4.0f && voxels.size <= 64.0f)) {
                problems.push_back(L"Voxels");
                continue;
            }
            pack.voxels = std::move(voxels);
        }
        else if (key == L"water" || key == L"effects") {
            auto parts = split(value, L'|');
            parts.resize(2);
            Shaded shaded{inside(parts[0]), ascii(parts[1])};
            if (shaded.file.empty()) {
                problems.push_back(key_as_written + L" " + value);
                continue;
            }
            (key == L"water" ? pack.water : pack.effects) = std::move(shaded);
        }
        else if (key == L"world") {
            auto parts = split(value, L'|');
            parts.resize(3);
            WorldPass world;
            world.file = inside(parts[0]);
            for (const auto &word : words(parts[1])) world.outputs.push_back(ascii(word));
            world.when = ascii(parts[2]);
            if (world.file.empty() || world.outputs.empty() || int(world.outputs.size()) > max_world_outputs) {
                problems.push_back(L"World " + value);
                continue;
            }
            pack.world = std::move(world);
        }
        else if (key == L"shadow") {
            auto parts = split(value, L'|');
            parts.resize(3);
            Shadow shadow;
            try {
                std::size_t used_size = 0, used_distance = 0;
                shadow.size = std::stoi(parts[0], &used_size);
                shadow.distance = std::stof(parts[1], &used_distance);
                if (used_size != parts[0].size() || used_distance != parts[1].size()) shadow.size = 0;
            }
            catch (...) {
                shadow.size = 0;
            }
            shadow.when = ascii(parts[2]);
            if (shadow.size < 256 || shadow.size > 4096 || !(shadow.distance >= 100.0f && shadow.distance <= 20000.0f)) {
                problems.push_back(L"Shadow");
                continue;
            }
            pack.shadow = std::move(shadow);
        }
        else if (key.starts_with(L"profile.")) {
            auto parts = split(value, L'|');
            parts.resize(3);
            Profile profile{ascii(key_as_written.substr(8)), parts[0], parts[1], {}};
            bool good = code_name(profile.id) && !(profile.russian.empty() && profile.english.empty());
            for (const auto &word : words(parts[2])) {
                const auto is = word.find(L'=');
                const std::string id = is == std::wstring::npos ? std::string() : ascii(word.substr(0, is));
                if (id.empty()) good = false;
                else profile.values.emplace_back(id, word.substr(is + 1));
            }
            if (!good || profile.values.empty()) {
                problems.push_back(L"Profile " + key_as_written.substr(8));
                continue;
            }
            pack.profiles.push_back(std::move(profile));
        }
        else if (key.starts_with(L"info.")) {
            auto parts = split(value, L'|');
            parts.resize(2);
            const std::string id = ascii(key_as_written.substr(5));
            auto &comment = pack.comments[id];
            comment.russian += (comment.russian.empty() ? L"" : L"\n") + parts[0];
            comment.english += (comment.english.empty() ? L"" : L"\n") + parts[1];
        }
    }
    // The profiles: each a setting of the pack at one of its values.
    for (const auto &profile : pack.profiles)
        for (const auto &[id, value] : profile.values) {
            const auto setting = std::ranges::find(pack.settings, id, &Mods::SettingLine::id);
            if (setting == pack.settings.end() || std::ranges::find(setting->values, value) == setting->values.end())
                problems.push_back(L"Profile " + utils::to_wstring(profile.id) + L": " + utils::to_wstring(id));
        }
    if (!pack.shadow.when.empty() &&
        std::ranges::find(pack.settings, pack.shadow.when, &Mods::SettingLine::id) == pack.settings.end())
        problems.push_back(L"Shadow: " + utils::to_wstring(pack.shadow.when));
    if (!pack.voxels.when.empty() &&
        std::ranges::find(pack.settings, pack.voxels.when, &Mods::SettingLine::id) == pack.settings.end())
        problems.push_back(L"Voxels: " + utils::to_wstring(pack.voxels.when));
    for (const auto &[shaded, name] : {std::pair{&pack.water, L"Water: "}, {&pack.effects, L"Effects: "}})
        if (!shaded->when.empty() &&
            std::ranges::find(pack.settings, shaded->when, &Mods::SettingLine::id) == pack.settings.end())
            problems.push_back(name + utils::to_wstring(shaded->when));
    // The world pass's targets: the pack's, each once, of the screen's size and one format (they are drawn into
    // together).
    if (!pack.world.file.empty()) {
        if (!pack.world.when.empty() &&
            std::ranges::find(pack.settings, pack.world.when, &Mods::SettingLine::id) == pack.settings.end())
            problems.push_back(L"World: " + utils::to_wstring(pack.world.when));
        const Target *first = nullptr;
        for (std::size_t i = 0; i < pack.world.outputs.size(); ++i) {
            const auto &name = pack.world.outputs[i];
            const auto target = std::ranges::find(pack.targets, name, &Target::name);
            const bool twice = std::find(pack.world.outputs.begin(), pack.world.outputs.begin() + i, name) !=
                               pack.world.outputs.begin() + i;
            if (target == pack.targets.end() || twice || target->scale != 1.0f || target->width != 0 ||
                (first && first->format != target->format)) {
                problems.push_back(L"World into " + text(name.c_str()));
                continue;
            }
            if (!first) first = &*target;
        }
    }
    // The screens' rows (the first: the profiles' row and all the settings, without Screen=); what an "i" is about.
    for (std::size_t i = 1; i < pack.screens.size(); ++i) resolve(pack, i, screen_rows[i - 1], problems);
    if (first_rows) resolve(pack, 0, *first_rows, problems);
    else {
        if (!pack.profiles.empty()) pack.screens[0].items.push_back({Item::Kind::Profile, -1});
        for (int i = 0; i < int(pack.settings.size()); ++i) pack.screens[0].items.push_back({Item::Kind::Setting, i});
    }
    for (const auto &[id, comment] : pack.comments)
        if (std::ranges::find(pack.settings, id, &Mods::SettingLine::id) == pack.settings.end() &&
            std::ranges::find(pack.notes, id, &Note::id) == pack.notes.end() && id != profile_row)
            problems.push_back(L"Info " + utils::to_wstring(id));
    if (pack.screens.size() > 64) problems.push_back(L"too many screens");
    // The names used once each, the passes' outputs declared, the last pass on the screen.
    std::vector<std::string> names;
    for (const auto &target : pack.targets) names.push_back(target.name);
    for (const auto &texture : pack.textures) names.push_back(texture.name);
    for (const auto &line : pack.settings) names.push_back(line.id);
    for (const auto &note : pack.notes) names.push_back(note.id);
    for (const auto &profile : pack.profiles) names.push_back("Profile " + profile.id);  // (among themselves)
    for (std::size_t i = 1; i < pack.screens.size(); ++i) names.push_back("[" + pack.screens[i].id + "]");
    std::ranges::sort(names);
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) problems.push_back(L"a name used twice");
    for (const auto &pass : pack.passes)
        if (pass.output != "screen" && std::ranges::find(pack.targets, pass.output, &Target::name) == pack.targets.end())
            problems.push_back(L"Pass into " + text(pass.output.c_str()));
    if (pack.passes.empty()) problems.push_back(L"no Pass");
    else if (pack.passes.back().output != "screen") problems.push_back(L"the last Pass is not into screen");
    if (int(pack.passes.size()) > max_passes) problems.push_back(L"too many passes");
    if (int(pack.targets.size()) > max_targets) problems.push_back(L"too many targets");
    if (int(pack.textures.size()) > max_textures) problems.push_back(L"too many textures");
    if (pack.name.empty()) pack.name = pack.name_en.empty() ? pack.id : pack.name_en;
    for (std::size_t i = 0; i < problems.size(); ++i) pack.problem += (i ? L", " : L"shaders.txt: ") + problems[i];
    return pack;
}

std::optional<Files> read_zip(std::string_view bytes, std::string &error) {
    // The end of the central directory: its signature within the last 64 KiB (the comment) and 22 bytes.
    if (bytes.size() < 22) {
        error = "not a zip";
        return std::nullopt;
    }
    std::size_t end = std::string_view::npos;
    const std::size_t lowest = bytes.size() > 22 + 65535 ? bytes.size() - 22 - 65535 : 0;
    for (std::size_t at = bytes.size() - 22 + 1; at-- > lowest;)
        if (u32(bytes, at) == 0x06054b50) {
            end = at;
            break;
        }
    if (end == std::string_view::npos) {
        error = "not a zip";
        return std::nullopt;
    }
    const std::uint32_t count = u16(bytes, end + 10);
    const std::uint32_t directory_size = u32(bytes, end + 12), directory = u32(bytes, end + 16);
    if (directory_size > bytes.size() || directory > bytes.size() - directory_size) {
        error = "a broken zip";
        return std::nullopt;
    }
    if (count > std::uint32_t(max_files)) {
        error = "too many files";
        return std::nullopt;
    }
    Files files;
    std::size_t total = 0;
    std::size_t at = directory;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at > bytes.size() || bytes.size() - at < 46 || u32(bytes, at) != 0x02014b50) {
            error = "a broken zip";
            return std::nullopt;
        }
        const std::uint32_t flags = u16(bytes, at + 8), method = u16(bytes, at + 10), crc = u32(bytes, at + 16);
        const std::uint32_t packed = u32(bytes, at + 20), size = u32(bytes, at + 24);
        const std::uint32_t name_length = u16(bytes, at + 28), extra = u16(bytes, at + 30), comment = u16(bytes, at + 32);
        const std::uint32_t local = u32(bytes, at + 42);
        if (bytes.size() - at - 46 < std::size_t(name_length) + extra + comment) {
            error = "a broken zip";
            return std::nullopt;
        }
        const std::string_view raw_name = bytes.substr(at + 46, name_length);
        at += 46 + std::size_t(name_length) + extra + comment;
        if (raw_name.ends_with('/')) continue;  // a folder
        std::wstring name;
        try {
            name = utils::to_wstring(raw_name);
        }
        catch (...) {
            name.assign(raw_name.begin(), raw_name.end());  // not UTF-8 (an old zip's code page): bytes as they are
        }
        name = inside(name);
        if (name.empty()) {
            error = "a file name climbing out of the pack";
            return std::nullopt;
        }
        if ((flags & 1) || (method != 0 && method != 8) || packed == 0xffffffffu || size == 0xffffffffu) {
            error = "a file the game cannot unpack (encrypted, zip64 or another method)";
            return std::nullopt;
        }
        if (size > max_file || (total += size) > max_total) {
            error = "files too large";
            return std::nullopt;
        }
        if (local > bytes.size() || bytes.size() - local < 30 || u32(bytes, local) != 0x04034b50) {
            error = "a broken zip";
            return std::nullopt;
        }
        const std::size_t data = std::size_t(local) + 30 + u16(bytes, local + 26) + u16(bytes, local + 28);
        if (data > bytes.size() || bytes.size() - data < packed) {
            error = "a broken zip";
            return std::nullopt;
        }
        std::optional<std::string> content;
        if (method == 0) content = packed == size ? std::optional<std::string>(bytes.substr(data, size)) : std::nullopt;
        else content = inflate_raw(bytes.substr(data, packed), size);
        if (!content ||
            crc32(0, reinterpret_cast<const Bytef *>(content->data()), uInt(content->size())) != crc) {
            error = "a broken file in the zip";
            return std::nullopt;
        }
        files[name] = std::move(*content);
    }
    return files;
}

std::optional<Files> load_files(const std::filesystem::path &place, std::string &error) {
    std::error_code failed;
    if (std::filesystem::is_directory(place, failed)) {
        Files files;
        std::size_t total = 0;
        int count = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(place, failed);
             !failed && it != std::filesystem::recursive_directory_iterator(); it.increment(failed)) {
            if (!it->is_regular_file(failed)) continue;
            if (++count > max_files) {
                error = "too many files";
                return std::nullopt;
            }
            const std::wstring name = inside(std::filesystem::relative(it->path(), place, failed).generic_wstring());
            auto bytes = read_file(it->path(), max_file);
            if (name.empty() || !bytes || (total += bytes->size()) > max_total) {
                error = "files too large or unreadable";
                return std::nullopt;
            }
            files[name] = std::move(*bytes);
        }
        return rooted(std::move(files), error);
    }
    const auto bytes = read_file(place, max_total);
    if (!bytes) {
        error = "cannot read it (or it is over 64 MB)";
        return std::nullopt;
    }
    auto files = read_zip(*bytes, error);
    if (!files) return std::nullopt;
    return rooted(std::move(*files), error);
}

std::vector<Pack> scan(const std::filesystem::path &shaders) {
    std::vector<Pack> packs;
    std::error_code failed;
    for (auto it = std::filesystem::directory_iterator(shaders, failed);
         !failed && it != std::filesystem::directory_iterator(); it.increment(failed)) {
        const std::wstring id = it->path().filename().wstring();
        const bool zip = lower(it->path().extension().wstring()) == L".zip";
        if (!(it->is_directory(failed) || (zip && it->is_regular_file(failed)))) continue;
        if (id.find_first_of(L"=\r\n") != std::wstring::npos) continue;  // CFG\shaders.txt could not name it
        std::string error;
        const auto files = load_files(it->path(), error);
        if (!files) {
            if (error.starts_with("no shaders.txt")) continue;  // not a pack of this game's
            Pack pack;
            pack.id = pack.name = id;
            pack.problem = text(error.c_str());
            packs.push_back(std::move(pack));
            continue;
        }
        Pack pack = parse(Mods::decode(files->at(description_file)), id);
        std::uint64_t stamp = 1469598103934665603ull;  // FNV-1a over the names and the bytes
        const auto mix = [&](const char *data, std::size_t size) {
            for (std::size_t i = 0; i < size; ++i) stamp = (stamp ^ std::uint8_t(data[i])) * 1099511628211ull;
        };
        for (const auto &[name, bytes] : *files) {
            mix(reinterpret_cast<const char *>(name.data()), name.size() * sizeof(wchar_t));
            mix(bytes.data(), bytes.size());
        }
        pack.stamp = stamp;
        for (const auto &pass : pack.passes)
            if (pack.problem.empty() && !files->contains(pass.file)) pack.problem = L"no " + pass.file;
        for (const auto &texture : pack.textures)
            if (pack.problem.empty() && !files->contains(texture.file)) pack.problem = L"no " + texture.file;
        for (const std::wstring *file : {&pack.world.file, &pack.water.file, &pack.effects.file})
            if (pack.problem.empty() && !file->empty() && !files->contains(*file)) pack.problem = L"no " + *file;
        for (const auto &pass : pack.passes)
            if (pack.problem.empty() && int(pass_inputs(pack, *files, pass).size()) > max_pass_pictures)
                pack.problem = pass.file + L" reads more than " + std::to_wstring(max_pass_pictures) +
                               L" targets and textures";
        packs.push_back(std::move(pack));
    }
    std::ranges::sort(packs, [](const Pack &a, const Pack &b) { return lower(a.name) < lower(b.name); });
    return packs;
}

namespace {
std::optional<std::vector<Pack>> g_installed;
}

const std::vector<Pack> &installed() {
    if (!g_installed) g_installed = scan(folder);
    return *g_installed;
}

void rescan() {
    auto packs = scan(folder);
    if (g_installed && packs == *g_installed) return;  // nothing changed: the built pack stays (no stutter)
    g_installed = std::move(packs);
    ++g_version;
}

std::optional<std::string> file(const Files &files, std::wstring_view name) {
    const std::wstring path = inside(name);
    const auto found = path.empty() ? files.end() : files.find(path);
    if (found == files.end()) return std::nullopt;
    return found->second;
}

namespace {
// What every code of a pack starts with: the settings as #defines.
std::string settings_code(const Pack &pack, const Mods::SettingValues &values) {
    // (On and Off: a switch's values, so `#if RAYS` reads it)
    std::string code = "// Planetary Battles Online: what the engine gives a pass (docs/SHADERS.md)\n#define PBO 1\n"
                       "#define On 1\n#define Off 0\n";
    for (const auto &line : pack.settings) {
        const std::string value = utils::from_wstring(setting_value(pack, line, values));
        // A value is a number or a word: anything else is left as 0 (a line of the code could not be closed).
        const bool plain = !value.empty() && std::ranges::all_of(value, [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '.' ||
                   ch == '-' || ch == '_';
        });
        code += "#define " + line.id + " " + (plain ? value : "0") + "\n";
    }
    return code;
}
}  // namespace

// The world pass: the game's own drawing sets the samplers (s0: the surface's first texture), so no pictures of
// the pack here; the constants a pass has that do not need the finished picture, in the same registers.
std::string world_prelude(const Pack &pack, const Mods::SettingValues &values) {
    return settings_code(pack, values) +
           "#define PBO_WORLD 1\n"
           "sampler2D WorldTex : register(s0);\n"
           "float4 Time : register(c2);\n"
           "float4 CameraPos : register(c3);\n"
           "float4 SunDir : register(c4);\n"
           "float4 SunColor : register(c5);\n"
           "float4 AmbientColor : register(c6);\n"
           "float4 MapInfo : register(c11);\n"
           "row_major float4x4 View : register(c12);\n"
           "row_major float4x4 Proj : register(c16);\n"
           "row_major float4x4 ViewProj : register(c20);\n"
           "float4 WorldInfo : register(c137);\n"
           "float4 LandInfo : register(c138);\n"
           "sampler2D PBO_LandNormals : register(s8);\n"
           "#define MAT_LAND 1\n"
           "#define MAT_OBJECT 2\n"
           "#define MAT_ROBOT 3\n"
           "#define MAT_BUILDING 4\n"
           "#define MAT_CANNON 5\n"
           "#define MAT_FLYER 6\n"
           "#define MAT_WATER 7\n"
           "float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }\n"
           // What main gets: the place in the world, the surface's normal there (the model's own, smooth; the
           // triangle's where a model has none), its texture's coordinates and colour there, the vertex's colour
           // (the land's baked light), what kind of thing it is (MAT_).
           "struct World {\n"
           "    float3 pos;\n"
           "    float3 normal;\n"
           "    float2 uv;\n"
           "    float4 color;\n"
           "    float4 tex;\n"
           "    float material;\n"
           // How far it moved on the screen since the previous frame, in the screen's uv (the camera's movement
           // and the thing's own: a robot's part by its place then): it was at this pixel's uv - motion.
           "    float2 motion;\n"
           "};\n";
}

std::string world_wrapper(const Pack &pack) {
    const int count = int(pack.world.outputs.size());
    std::string code =
        "\nstruct PBO_WorldIn {\n"
        "    float3 pos : TEXCOORD0;\n"
        "    float3 normal : TEXCOORD1;\n"
        "    float2 uv : TEXCOORD2;\n"
        "    float4 now : TEXCOORD3;\n"
        "    float4 before : TEXCOORD4;\n"
        "    float4 color : COLOR0;\n"
        "};\n"
        "struct PBO_WorldOut {\n";
    for (int i = 0; i < count; ++i)
        code += "    float4 c" + std::to_string(i) + " : COLOR" + std::to_string(i) + ";\n";
    code +=
        "};\n"
        "PBO_WorldOut pbo_world(PBO_WorldIn i) {\n"
        "    World w;\n"
        "    w.pos = i.pos;\n"
        // (a model without normals: the triangle's own, towards the camera)
        "    float3 flat = cross(ddy(i.pos), ddx(i.pos));\n"
        "    flat = dot(flat, CameraPos.xyz - i.pos) < 0 ? -flat : flat;\n"
        "    float3 n = dot(i.normal, i.normal) > 1e-6 ? i.normal : flat;\n"
        // (the land: the map's own normals at its points, smooth; its mesh has none or coarser ones)
        "    if (WorldInfo.y > 0.5 && abs(WorldInfo.x - MAT_LAND) < 0.5)\n"
        "        n = tex2Dlod(PBO_LandNormals, float4((i.pos.xy * LandInfo.x + 0.5) * LandInfo.zw, 0, 0)).rgb * 2.0 - 1.0;\n"
        "    w.normal = n * rsqrt(max(dot(n, n), 1e-12));\n"
        "    w.uv = i.uv;\n"
        "    w.color = i.color;\n"
        "    w.tex = tex2D(WorldTex, i.uv);\n"
        "    w.material = WorldInfo.x;\n"
        // (behind the camera then: no place on the screen to come from)
        "    w.motion = i.now.w > 1e-4 && i.before.w > 1e-4\n"
        "                       ? (i.now.xy / i.now.w - i.before.xy / i.before.w) * float2(0.5, -0.5)\n"
        "                       : float2(0, 0);\n"
        "    PBO_WorldOut o;\n"
        "    main(w";
    for (int i = 0; i < count; ++i) code += ", o.c" + std::to_string(i);
    code +=
        ");\n"
        // The game's alpha test goes by the first target's alpha: the texture's (leaves stay cut out).
        "    o.c0.a = w.tex.a;\n"
        "    return o;\n"
        "}\n";
    return code;
}

namespace {
// What the water's and the effects' code share with a pass's: the constants of the same names in the same registers
// (no lights, no shadow map: those are the passes').
constexpr const char *view_constants =
    "float4 SceneSize : register(c1);\n"
    "float4 Time : register(c2);\n"
    "float4 CameraPos : register(c3);\n"
    "float4 SunDir : register(c4);\n"
    "float4 SunColor : register(c5);\n"
    "float4 AmbientColor : register(c6);\n"
    "float4 FogColor : register(c7);\n"
    "float4 FogRange : register(c8);\n"
    "float4 DepthParams : register(c9);\n"
    "float4 MapInfo : register(c11);\n"
    "row_major float4x4 View : register(c12);\n"
    "row_major float4x4 Proj : register(c16);\n"
    "row_major float4x4 ViewProj : register(c20);\n"
    "row_major float4x4 InvViewProj : register(c24);\n"
    "#define MAT_LAND 1\n#define MAT_OBJECT 2\n#define MAT_ROBOT 3\n#define MAT_BUILDING 4\n#define MAT_CANNON 5\n"
    "#define MAT_FLYER 6\n#define MAT_WATER 7\n"
    "float ViewDepth(float d) { return DepthParams.y / (d - DepthParams.x); }\n"
    "float3 WorldPos(float2 uv, float d) {\n"
    "    float4 p = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1), InvViewProj);\n"
    "    return p.xyz / p.w;\n"
    "}\n"
    "float3 ScreenPos(float3 p) {\n"
    "    float4 c = mul(float4(p, 1), ViewProj);\n"
    "    c.xyz /= c.w;\n"
    "    return float3(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5, c.z);\n"
    "}\n"
    "float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }\n";
}  // namespace

std::string water_prelude(const Pack &pack, const Mods::SettingValues &values) {
    return settings_code(pack, values) +
           "#define PBO_WATER 1\n"
           "sampler2D PBO_WaterMask : register(s0);\n"
           "sampler2D WaterTex : register(s1);\n"
           "sampler2D ReflTex : register(s2);\n"
           "sampler2D UnderTex : register(s3);\n"
           "sampler2D DepthTex : register(s4);\n" +
           view_constants +
           "float SceneDepth(float2 uv) { return tex2Dlod(DepthTex, float4(uv, 0, 0)).r; }\n"
           // What main gets (docs/SHADERS.md, The water).
           "struct Water {\n"
           "    float3 pos;\n"
           "    float3 normal;\n"
           "    float2 uv;\n"
           "    float mask;\n"
           "    float4 tex;\n"
           "    float4 refl;\n"
           "    float3 light;\n"
           "    float4 color;\n"
           "    float2 screen;\n"
           "    float3 under;\n"
           "    float depth;\n"
           "    float fog;\n"
           "};\n";
}

std::string water_wrapper() {
    return "\nstruct PBO_WaterIn {\n"
           "    float3 pos : TEXCOORD0;\n"
           "    float3 normal : TEXCOORD1;\n"
           "    float2 mask : TEXCOORD2;\n"
           "    float2 uv : TEXCOORD3;\n"
           "    float2 refl : TEXCOORD4;\n"
           "    float4 light : TEXCOORD5;\n"
           "    float2 vpos : VPOS;\n"
           "};\n"
           "float4 pbo_water(PBO_WaterIn i) : COLOR {\n"
           "    Water w;\n"
           "    w.pos = i.pos;\n"
           "    w.normal = normalize(i.normal);\n"
           "    w.uv = i.uv;\n"
           "    w.mask = tex2D(PBO_WaterMask, i.mask).a;\n"
           "    w.tex = tex2D(WaterTex, i.uv);\n"
           "    w.refl = tex2D(ReflTex, i.refl);\n"
           "    w.light = i.light.rgb;\n"
           // The game's own: its texture by its light (twice), its reflection over that by the reflection's alpha.
           "    float3 c = saturate(w.tex.rgb * w.light * 2.0);\n"
           "    w.color = float4(lerp(c, w.refl.rgb, w.refl.a), w.mask);\n"
           "    w.screen = (i.vpos + 0.5) * SceneSize.zw;\n"
           "    w.under = WorldPos(w.screen, SceneDepth(w.screen));\n"
           "    w.depth = max(i.pos.z - w.under.z, 0.0);\n"
           "    w.fog = i.light.a;\n"
           "    float4 o = main(w);\n"
           "    o.rgb = lerp(o.rgb, FogColor.rgb, w.fog);\n"
           "    return o;\n"
           "}\n";
}

std::string effects_prelude(const Pack &pack, const Mods::SettingValues &values) {
    return settings_code(pack, values) +
           "#define PBO_EFFECT 1\n"
           "sampler2D DepthTex : register(s14);\n"
           "sampler2D SolidTex : register(s15);\n"
           "float4 PBO_TFactor : register(c0);\n"
           "float4 EffectInfo : register(c10);\n" +
           view_constants +
           "float SceneDepth(float2 uv) { return tex2D(DepthTex, uv).r; }\n"
           // What main gets (docs/SHADERS.md, The effects).
           "struct Effect {\n"
           "    float4 color;\n"
           "    float4 diffuse;\n"
           "    float4 tex;\n"
           "    float2 uv;\n"
           "    float2 screen;\n"
           "    float depth;\n"
           "    float placed;\n"
           "    float facing;\n"
           "};\n"
           // The effect's colour with k of what it lays on the picture (1 all, 0 nothing), whichever way the game
           // lays it: by its alpha (over, or added by its alpha), or its colour added as it is.
           "float4 Faded(float4 c, float k) {\n"
           "    return float4(c.rgb * lerp(1.0, k, EffectInfo.y), c.a * lerp(1.0, k, EffectInfo.z));\n"
           "}\n";
}

std::string effects_wrapper(const Stages &stages, const Combiner &combine) {
    std::string code =
        "\nstruct PBO_EffectIn {\n"
        "    float4 diffuse : COLOR0;\n"
        "    float4 specular : COLOR1;\n";
    for (int s = 0; s < combine.stages; ++s)
        code += "    float4 uv" + std::to_string(s) + " : TEXCOORD" + std::to_string(s) + ";\n";
    code += "    float4 clip : TEXCOORD7;\n"
            "};\n";
    for (int s = 0; s < combine.stages; ++s)
        code += std::string(stages[s].cube ? "samplerCUBE" : "sampler2D") + " PBO_Stage" + std::to_string(s) +
                " : register(s" + std::to_string(s) + ");\n";
    code += "float4 pbo_effect(PBO_EffectIn i) : COLOR {\n";
    for (int s = 0; s < combine.stages; ++s) {
        const std::string n = std::to_string(s);
        code += "    float4 t" + n + " = " + (stages[s].cube ? "texCUBE(PBO_Stage" + n + ", i.uv" + n + ".xyz)"
                                                             : "tex2D(PBO_Stage" + n + ", i.uv" + n + ".xy)") + ";\n";
    }
    code += combine.code +
            "    Effect e;\n"
            "    e.color = cur;\n"
            "    e.diffuse = i.diffuse;\n" +
            std::string(combine.stages > 0 ? "    e.tex = t0;\n    e.uv = i.uv0.xy;\n"
                                           : "    e.tex = float4(1, 1, 1, 1);\n    e.uv = float2(0, 0);\n") +
            "    float w = max(i.clip.w, 1e-6);\n"
            "    e.screen = (i.clip.xy / w) * float2(0.5, -0.5) + 0.5;\n"
            "    e.depth = i.clip.z / w;\n"
            "    e.placed = EffectInfo.x;\n"
            "    e.facing = EffectInfo.w;\n"
            "    return main(e);\n"
            "}\n";
    return code;
}

std::optional<Combiner> combiner(const Stages &stages) {
    Combiner result;
    std::string code = "    float4 cur = i.diffuse;\n";
    for (int s = 0; s < effect_stages; ++s) {
        const Stage &stage = stages[s];
        if (stage.color_op == D3DTOP_DISABLE) break;
        const std::string t = "t" + std::to_string(s);
        bool reads_texture = false;
        // An argument's value (".rgb" or ".a" of it), its modifiers.
        const auto argument = [&](unsigned arg, const char *part) -> std::optional<std::string> {
            std::string base;
            switch (arg & D3DTA_SELECTMASK) {
                case D3DTA_DIFFUSE: base = "i.diffuse"; break;
                case D3DTA_CURRENT: base = "cur"; break;
                case D3DTA_TEXTURE: base = t; reads_texture = true; break;
                case D3DTA_TFACTOR: base = "PBO_TFactor"; break;
                case D3DTA_SPECULAR: base = "i.specular"; break;
                default: return std::nullopt;
            }
            if (arg & ~(D3DTA_SELECTMASK | D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE)) return std::nullopt;
            std::string value = (arg & D3DTA_ALPHAREPLICATE) ? base + ".aaaa" : base;
            value += part;
            if (arg & D3DTA_COMPLEMENT) value = "(1.0 - " + value + ")";
            return value;
        };
        const auto operation = [&](unsigned op, unsigned arg1, unsigned arg2, const char *part) -> std::optional<std::string> {
            const auto a = argument(arg1, part), b = argument(arg2, part);
            const auto blend = [&](const std::string &by) -> std::optional<std::string> {
                if (!a || !b) return std::nullopt;
                return "lerp(" + *b + ", " + *a + ", " + by + ")";
            };
            switch (op) {
                case D3DTOP_SELECTARG1: return a;
                case D3DTOP_SELECTARG2: return b;
                default: break;
            }
            if (!a || !b) return std::nullopt;
            switch (op) {
                case D3DTOP_MODULATE: return *a + " * " + *b;
                case D3DTOP_MODULATE2X: return "(" + *a + " * " + *b + ") * 2.0";
                case D3DTOP_MODULATE4X: return "(" + *a + " * " + *b + ") * 4.0";
                case D3DTOP_ADD: return *a + " + " + *b;
                case D3DTOP_ADDSIGNED: return *a + " + " + *b + " - 0.5";
                case D3DTOP_ADDSIGNED2X: return "(" + *a + " + " + *b + " - 0.5) * 2.0";
                case D3DTOP_SUBTRACT: return *a + " - " + *b;
                case D3DTOP_ADDSMOOTH: return *a + " + " + *b + " - " + *a + " * " + *b;
                case D3DTOP_BLENDDIFFUSEALPHA: return blend("i.diffuse.a");
                case D3DTOP_BLENDTEXTUREALPHA: reads_texture = true; return blend(t + ".a");
                case D3DTOP_BLENDFACTORALPHA: return blend("PBO_TFactor.a");
                case D3DTOP_BLENDCURRENTALPHA: return blend("cur.a");
                default: return std::nullopt;
            }
        };
        const auto color = operation(stage.color_op, stage.color1, stage.color2, ".rgb");
        // (an alpha left out while the colour goes on: the alpha as it was)
        const auto alpha = stage.alpha_op == D3DTOP_DISABLE
                                   ? std::optional<std::string>("cur.a")
                                   : operation(stage.alpha_op, stage.alpha1, stage.alpha2, ".a");
        if (!color || !alpha) return std::nullopt;
        // Each stage's result within 0 and 1, as the game's.
        code += "    cur = saturate(float4(" + *color + ", " + *alpha + "));\n";
        result.stages = s + 1;
        (void)reads_texture;
    }
    result.code = code;
    return result;
}

std::string prelude(const Pack &pack, const Mods::SettingValues &values, const Inputs &inputs) {
    std::string code = settings_code(pack, values);
    code +=
        "sampler2D SceneTex : register(s0);\n"
        "sampler2D DepthTex : register(s1);\n"
        "sampler2D PrevTex : register(s2);\n"
        "sampler2D SolidTex : register(s15);\n";
    // The pictures this pass reads, from s3 on (SolidTex has the last).
    int slot = 3;
    for (int index : inputs.targets)
        code += "sampler2D " + pack.targets[index].name + " : register(s" + std::to_string(slot++) + ");\n";
    for (int index : inputs.textures)
        code += "sampler2D " + pack.textures[index].name + " : register(s" + std::to_string(slot++) + ");\n";
    // The shadow map for a pass that reads it: where the light's view of the world ends there (0 at the light's eye,
    // 1 nothing). ShadowLit: whether the light reaches a point (1) or something stands before it (0); `bias`: how
    // many of the world's units nearer the light the point is taken (a surface does not shade itself). 1 outside the
    // map and when there is none (ShadowInfo.w 0). A pass that reads none (a pack without Shadow=): always lit.
    if (inputs.shadow)
        code += "sampler2D ShadowTex : register(s" + std::to_string(slot++) + ");\n"
                "float ShadowDepth(float2 uv) { return tex2Dlod(ShadowTex, float4(uv, 0, 0)).r; }\n";
    else
        code += "float ShadowDepth(float2 uv) { return 1; }\n";
    // The voxel copy for a pass that reads it; VoxelAt: what stands at a place of the world (0 nothing, else
    // MAT_: 1 the land...), 0 outside the map and when there is no copy (VoxelScale.w 0). No branches: for a loop
    // along a ray.
    if (inputs.voxels) code += "sampler3D VoxelTex : register(s" + std::to_string(slot++) + ");\n";
    code +=
        "float4 VoxelOrigin : register(c139);\n"
        "float4 VoxelScale : register(c140);\n"
        "float4 VoxelExtent : register(c141);\n"
        "#define MAT_LAND 1\n"
        "#define MAT_OBJECT 2\n"
        "#define MAT_ROBOT 3\n"
        "#define MAT_BUILDING 4\n"
        "#define MAT_CANNON 5\n"
        "#define MAT_FLYER 6\n"
        "#define MAT_WATER 7\n";
    if (inputs.voxels)
        code += "float VoxelAt(float3 p) {\n"
                "    float3 q = p - VoxelOrigin.xyz;\n"
                "    float3 over = q - VoxelExtent.xyz;\n"
                "    float inside = step(0.0, min(q.x, min(q.y, q.z))) * step(max(over.x, max(over.y, over.z)), 0.0) *\n"
                "                   VoxelScale.w;\n"
                "    return floor(tex3Dlod(VoxelTex, float4(q * VoxelScale.xyz, 0)).r * 255.0 + 0.5) * inside;\n"
                "}\n";
    else
        code += "float VoxelAt(float3 p) { return 0; }\n";
    code +=
        "float4 ScreenSize : register(c0);\n"
        "float4 SceneSize : register(c1);\n"
        "float4 Time : register(c2);\n"
        "float4 CameraPos : register(c3);\n"
        "float4 SunDir : register(c4);\n"
        "float4 SunColor : register(c5);\n"
        "float4 AmbientColor : register(c6);\n"
        "float4 FogColor : register(c7);\n"
        "float4 FogRange : register(c8);\n"
        "float4 DepthParams : register(c9);\n"
        "float4 State : register(c10);\n"
        "float4 MapInfo : register(c11);\n"
        "row_major float4x4 View : register(c12);\n"
        "row_major float4x4 Proj : register(c16);\n"
        "row_major float4x4 ViewProj : register(c20);\n"
        "row_major float4x4 InvViewProj : register(c24);\n"
        "row_major float4x4 PrevViewProj : register(c28);\n"
        "float4 LightCount : register(c32);\n"
        "float4 LightPos[32] : register(c33);\n"
        "float4 LightColor[32] : register(c65);\n"
        "float4 LightInfo[32] : register(c97);\n"
        "float4 ShadowColor : register(c129);\n"
        "float4 SkyHorizon : register(c130);\n"
        "float4 PrevCameraPos : register(c131);\n"
        "row_major float4x4 ShadowViewProj : register(c132);\n"
        "float4 ShadowInfo : register(c136);\n"
        "#define MAX_LIGHTS 32\n"
        "#define LIGHT_GLOW 0\n"
        "#define LIGHT_FIRE 1\n"
        "#define LIGHT_FLASH 2\n"
        "#define LIGHT_SHOT 3\n"
        "float ViewDepth(float d) { return DepthParams.y / (d - DepthParams.x); }\n"
        "float SceneDepth(float2 uv) { return tex2Dlod(DepthTex, float4(uv, 0, 0)).r; }\n"
        "float3 WorldPos(float2 uv, float d) {\n"
        "    float4 p = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1), InvViewProj);\n"
        "    return p.xyz / p.w;\n"
        "}\n"
        "float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }\n"
        // (the map's pixel centres on its texels': Direct3D 9's half pixel)
        "float3 ShadowPos(float3 p) {\n"
        "    float4 c = mul(float4(p, 1), ShadowViewProj);\n"
        "    return float3(c.x * 0.5 + 0.5 + ShadowInfo.y * 0.5, 0.5 - c.y * 0.5 + ShadowInfo.y * 0.5, c.z);\n"
        "}\n"
        // (no branches: it is called in loops, many times a pixel)
        "float ShadowLit(float3 p, float bias) {\n"
        "    float3 s = ShadowPos(p);\n"
        "    float lit = step(s.z - bias / max(ShadowInfo.w, 1e-6), ShadowDepth(s.xy));\n"
        "    float inside = step(0.0, min(s.x, s.y)) * step(max(s.x, s.y), 1.0) * step(s.z, 0.99999) *\n"
        "                   step(1e-6, ShadowInfo.w);\n"
        "    return lerp(1.0, lit, inside);\n"
        "}\n"
        "float3 ScreenPos(float3 p) {\n"
        "    float4 c = mul(float4(p, 1), ViewProj);\n"
        "    c.xyz /= c.w;\n"
        "    return float3(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5, c.z);\n"
        "}\n"
        // A smaller pass's pixel centre lies on the corner of four screen pixels: which one the (point) depth gives
        // was left to rounding, row by row, and what was built of it jumped in lines across the screen.
        "float2 ScenePixel(float2 uv) { return (floor(uv * SceneSize.xy - 0.25) + 0.5) * SceneSize.zw; }\n"
        "float3 SceneNormal(float2 uv) {\n"
        "    uv = ScenePixel(uv);\n"
        "    float d = SceneDepth(uv);\n"
        "    if (d >= 1) return float3(0, 0, 1);\n"
        "    float2 px = SceneSize.zw;\n"
        "    float3 p = WorldPos(uv, d);\n"
        "    float z = ViewDepth(d);\n"
        "    float dl = SceneDepth(uv - float2(px.x, 0)), dr = SceneDepth(uv + float2(px.x, 0));\n"
        "    float du = SceneDepth(uv - float2(0, px.y)), dd = SceneDepth(uv + float2(0, px.y));\n"
        "    float3 h = abs(ViewDepth(dr) - z) < abs(z - ViewDepth(dl)) ? WorldPos(uv + float2(px.x, 0), dr) - p\n"
        "                                                            : p - WorldPos(uv - float2(px.x, 0), dl);\n"
        "    float3 v = abs(ViewDepth(dd) - z) < abs(z - ViewDepth(du)) ? WorldPos(uv + float2(0, px.y), dd) - p\n"
        "                                                            : p - WorldPos(uv - float2(0, px.y), du);\n"
        "    float3 c = cross(h, v);\n"
        "    float l2 = dot(c, c);\n"
        "    if (!(l2 > 1e-12)) return float3(0, 0, 1);\n"
        "    float3 n = c * rsqrt(l2);\n"
        "    return dot(n, CameraPos.xyz - p) < 0 ? -n : n;\n"
        "}\n"
        // Generated normals: the picture's own light and dark taken for height (as a Minecraft pack's do from a
        // block's texture; the game draws without shaders, so the picture before the effects stands in for the
        // textures: the game's shadow taken out of it first, its edge is no relief).
        "float SceneHeight(float2 uv) {\n"
        "    float4 c = tex2Dlod(SolidTex, float4(uv, 0, 0));\n"
        "    float lay = State.w > 0.5 ? (1.0 - c.a) * ShadowColor.a : 0.0;\n"
        "    float3 bare = lay > 0.001 ? max(c.rgb - ShadowColor.rgb * lay, 0) / max(1.0 - lay, 0.05) : c.rgb;\n"
        "    return Luma(bare);\n"
        "}\n"
        // How the height climbs along the surface there, per unit of the world (a vector in the surface towards
        // the brighter; 0 on the sky and across an edge of things). From the pixels on both sides (one pixel's
        // difference is mostly the picture's grain).
        "float3 ReliefSlope(float2 uv) {\n"
        "    uv = ScenePixel(uv);\n"
        "    float d = SceneDepth(uv);\n"
        "    if (d >= 1) return float3(0, 0, 0);\n"
        "    float2 px = SceneSize.zw;\n"
        "    float2 ul = uv - float2(px.x, 0), ur = uv + float2(px.x, 0);\n"
        "    float2 uu = uv - float2(0, px.y), ud = uv + float2(0, px.y);\n"
        "    float dl = SceneDepth(ul), dr = SceneDepth(ur), du = SceneDepth(uu), dd = SceneDepth(ud);\n"
        "    float z = ViewDepth(d), same = 0.5 + z * 0.02;\n"
        "    if (abs(ViewDepth(dl) - z) > same || abs(ViewDepth(dr) - z) > same || abs(ViewDepth(du) - z) > same ||\n"
        "        abs(ViewDepth(dd) - z) > same)\n"
        "        return float3(0, 0, 0);\n"
        "    float3 across = WorldPos(ur, dr) - WorldPos(ul, dl), down = WorldPos(ud, dd) - WorldPos(uu, du);\n"
        "    return across * ((SceneHeight(ur) - SceneHeight(ul)) / max(dot(across, across), 1e-6)) +\n"
        "           down * ((SceneHeight(ud) - SceneHeight(uu)) / max(dot(down, down), 1e-6));\n"
        "}\n"
        // The surface's normal bent by the relief (strength: how deep it is taken to be, 1 a moderate one).
        "float3 GeneratedNormal(float2 uv, float strength) {\n"
        "    float3 n = SceneNormal(uv);\n"
        "    if (strength <= 0) return n;\n"
        "    return normalize(n - ReliefSlope(uv) * (strength * 6.0));\n"
        "}\n";
    return code;
}

Choice load_choice(const std::filesystem::path &path) {
    Choice choice;
    auto values = Mods::load_settings(path);
    if (const auto on = values.find("Shaders.On"); on != values.end()) choice.on = on->second == L"1";
    if (const auto pack = values.find("Shaders.Pack"); pack != values.end()) choice.pack = pack->second;
    values.erase("Shaders.On");
    values.erase("Shaders.Pack");
    choice.settings = std::move(values);
    return choice;
}

bool save_choice(const std::filesystem::path &path, const Choice &choice, std::string &error) {
    Mods::SettingValues values = choice.settings;
    values["Shaders.On"] = choice.on ? L"1" : L"0";
    values["Shaders.Pack"] = choice.pack;
    return Mods::save_settings(path, values, error, "; Planetary Battles Online: this person's shaders (the settings' Shaders page)");
}

const Choice &choice() {
    if (!g_choice) g_choice = load_choice(choice_file);
    return *g_choice;
}

void set_choice(Choice choice) {
    g_choice = std::move(choice);
    ++g_version;
}

std::uint32_t choice_version() { return g_version; }

}  // namespace ShaderPacks
