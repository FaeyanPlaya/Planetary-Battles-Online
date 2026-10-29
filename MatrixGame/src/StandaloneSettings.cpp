// MatrixGame - licensed under GPLv2 or any later version.
#include "StandaloneSettings.hpp"
#include "RandomConfiguration.hpp"
#include "MapCatalog.hpp"
#include "Mods.hpp"

#include "CBlockPar.hpp"
#include "CException.hpp"
#include "utils.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace StandaloneSettings {
namespace {

constexpr std::array keys{L"RandomGenerator", L"ResolutionWidth", L"ResolutionHeight", L"WindowMode",
                          L"FullscreenMode",  L"SoundVolume",     L"StencilShadows",   L"ProjectedShadows",
                          L"Skybox",          L"RobotShadows",    L"CameraSpeed",      L"DelayedDamage",
                          L"Map",             L"GroupCounts",      L"BattleBonus",      L"PlaceContainment",
                          L"ZeroXTrace",      L"MusicVolume",      L"Mods",             L"PersonalMods",
                          L"FarViewScale"};

std::optional<std::wstring> single(const Base::CBlockPar &options, const wchar_t *key) {
    if (options.ParCount(key) != 1) return std::nullopt;
    auto value = utils::trim(options.ParGet(key));
    utils::to_lower(value);
    return value;
}

std::optional<int> percent(const std::wstring &value, int minimum, int maximum) {
    if (value.empty() || value.size() > 3) return std::nullopt;
    int number = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return std::nullopt;
        number = number * 10 + (ch - L'0');
    }
    if (number < minimum || number > maximum) return std::nullopt;
    return number;
}

std::optional<bool> boolean(const std::wstring &value) {
    if (value == L"on" || value == L"true" || value == L"1") return true;
    if (value == L"off" || value == L"false" || value == L"0") return false;
    return std::nullopt;
}

std::optional<Skybox> skybox(const std::wstring &value) {
    if (value == L"off") return Skybox::Off;
    if (value == L"low") return Skybox::Low;
    if (value == L"high") return Skybox::High;
    return std::nullopt;
}

std::optional<DamageAttribution> delayed_damage(const std::wstring &value) {
    if (value == L"corrected") return DamageAttribution::Corrected;
    if (value == L"original") return DamageAttribution::Original;
    return std::nullopt;
}

std::optional<GroupCountMode> group_counts(const std::wstring &value) {
    if (value == L"corrected") return GroupCountMode::Corrected;
    if (value == L"original") return GroupCountMode::Original;
    return std::nullopt;
}

std::optional<BattleChoice::Bonus> battle_bonus(const std::wstring &value) {
    if (value == L"none") return BattleChoice::Bonus::None;
    if (value == L"airsupport") return BattleChoice::Bonus::AirSupport;
    if (value == L"reinforcedarmor") return BattleChoice::Bonus::ReinforcedArmor;
    return std::nullopt;
}

// Default, None or mod ids separated by commas.
std::optional<std::optional<std::vector<std::string>>> mod_list(const std::wstring &value) {
    if (value == L"default") return std::optional<std::vector<std::string>>{};
    if (value == L"none") return std::optional<std::vector<std::string>>{std::vector<std::string>{}};
    std::vector<std::string> ids;
    std::wstring_view list = value;
    while (!list.empty()) {
        const auto comma = list.find(L',');
        const std::string id = Mods::id_of(utils::trim(std::wstring(list.substr(0, comma))));
        list = comma == std::wstring_view::npos ? std::wstring_view{} : list.substr(comma + 1);
        if (id.empty()) return std::nullopt;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    if (ids.empty()) return std::nullopt;
    return std::optional<std::vector<std::string>>{ids};
}

std::optional<PlaceContainmentMode> place_containment(const std::wstring &value) {
    if (value == L"corrected") return PlaceContainmentMode::Corrected;
    if (value == L"original") return PlaceContainmentMode::Original;
    return std::nullopt;
}

std::optional<ZeroXTraceMode> zero_x_trace(const std::wstring &value) {
    if (value == L"corrected") return ZeroXTraceMode::Corrected;
    if (value == L"original") return ZeroXTraceMode::Original;
    return std::nullopt;
}

// Reads an optional key; an absent key keeps the default, a present invalid key is reported.
template<class T, class Parse>
void read(const Base::CBlockPar &options, const wchar_t *key, T &target, Parse parse, Selection &selection) {
    if (!options.ParCount(key)) return;
    const auto value = single(options, key);
    const auto parsed = value ? parse(*value) : decltype(parse(std::wstring{})){};
    if (parsed) target = *parsed;
    else selection.invalid.push_back(utils::from_wstring(key));
}

// An empty value keeps the configured map; anything else must be a path the map catalogue accepts.
std::optional<std::wstring> map_path(const Base::CBlockPar &options) {
    if (options.ParCount(L"Map") != 1) return std::nullopt;
    auto value = utils::trim(options.ParGet(L"Map"));
    if (!value.empty() && !MapCatalog::valid_path(value)) return std::nullopt;
    return value;
}

std::wstring value_text(const Values &values, std::wstring_view key) {
    const auto on_off = [](bool value) { return std::wstring(value ? L"On" : L"Off"); };
    if (key == L"RandomGenerator") return utils::to_wstring(RandomConfiguration::name(values.random));
    if (key == L"ResolutionWidth") return values.display.width ? std::to_wstring(values.display.width) : L"Auto";
    if (key == L"ResolutionHeight") return values.display.height ? std::to_wstring(values.display.height) : L"Auto";
    if (key == L"WindowMode") return utils::to_wstring(Display::name(values.display.mode));
    if (key == L"FullscreenMode") return utils::to_wstring(Display::name(values.display.fullscreen));
    if (key == L"SoundVolume") return std::to_wstring(values.sound_volume);
    if (key == L"StencilShadows") return on_off(values.stencil_shadows);
    if (key == L"ProjectedShadows") return on_off(values.projected_shadows);
    if (key == L"Skybox") return utils::to_wstring(name(values.skybox));
    if (key == L"RobotShadows") return on_off(values.robot_shadows);
    if (key == L"CameraSpeed") return std::to_wstring(values.camera_speed);
    if (key == L"DelayedDamage") return utils::to_wstring(name(values.delayed_damage));
    if (key == L"BattleBonus") {
        switch (values.battle_bonus) {
            case BattleChoice::Bonus::AirSupport: return L"AirSupport";
            case BattleChoice::Bonus::ReinforcedArmor: return L"ReinforcedArmor";
            default: return L"None";
        }
    }
    if (key == L"Map") return values.map;
    if (key == L"GroupCounts") return utils::to_wstring(name(values.group_counts));
    if (key == L"PlaceContainment") return utils::to_wstring(name(values.place_containment));
    if (key == L"ZeroXTrace") return utils::to_wstring(name(values.zero_x_trace));
    if (key == L"MusicVolume") return std::to_wstring(values.music_volume);
    if (key == L"FarViewScale") return std::to_wstring(values.far_view_scale);
    if (key == L"Mods" || key == L"PersonalMods") {
        const auto &mods = key == L"Mods" ? values.mods : values.personal_mods;
        if (!mods) return L"Default";
        return mods->empty() ? L"None" : utils::to_wstring(Mods::join(*mods));
    }
    throw std::logic_error("Unknown settings key");
}

bool space(wchar_t ch) { return ch == L' ' || ch == L'\t'; }

size_t line_start(const std::wstring &text, size_t position) {
    while (position > 0 && text[position - 1] != L'\n') --position;
    return position;
}

// A top-level option as CBlockPar reads it: the name ends at '=' and the value at the comment.
struct Option {
    size_t begin, equals, end;
};

// Follows CBlockPar's statement rules to find the options it reads at the top level. Text after
// a block's closing brace on the same line is a statement of the enclosing level, and a closing
// brace at the top level ends the reader's input.
class Scanner {
public:
    explicit Scanner(const std::wstring &source) : stop(source.size()), text(source) { scan(0, true); }

    std::vector<Option> options;
    size_t stop;  // Position of a top-level closing brace, or the end of the text.

private:
    const std::wstring &text;
    size_t pos = 0;
    size_t line_end = 0;
    size_t code_end = 0;  // End of the statements; a comment follows when it precedes line_end.

    void skip_space() {
        while (pos < text.size() && space(text[pos])) ++pos;
    }
    void find_line() {
        line_end = pos;
        while (line_end < text.size() && text[line_end] != L'\n' && text[line_end] != L'\r') ++line_end;
        size_t comment = pos;
        while (comment + 1 < line_end && (text[comment] != L'/' || text[comment + 1] != L'/')) ++comment;
        if (comment + 1 >= line_end) {
            code_end = line_end;
            return;
        }
        code_end = comment;
        while (code_end > pos && space(text[code_end - 1])) --code_end;
    }
    bool next_line() {
        pos = line_end;
        while (pos < text.size() && text[pos] != L'\n') ++pos;
        if (pos == text.size()) return false;
        ++pos;
        return true;
    }
    size_t find(wchar_t ch) const {
        for (size_t i = pos; i < code_end; ++i) {
            if (text[i] == ch) return i;
        }
        return std::wstring::npos;
    }

    size_t scan(size_t start, bool top) {
        pos = start;
        skip_space();
        find_line();
        while (pos < text.size()) {
            if (pos >= code_end) {
                // An empty or comment-only line.
            }
            else if (text[pos] == L'}') {
                if (top) stop = pos;
                return pos + 1;
            }
            else if (const size_t open = find(L'{'); open != std::wstring::npos) {
                const bool comment = code_end < line_end;
                size_t close = open + 1;
                while (close < code_end && space(text[close])) ++close;
                if (close < code_end && text[close] == L'}') {
                    if (!comment || close + 1 < code_end) {
                        pos = close + 1;
                        skip_space();
                        if (pos < line_end) continue;
                    }
                }
                else {
                    if (comment && open + 1 >= code_end) {
                        if (!next_line()) break;
                    }
                    else {
                        pos = open + 1;
                        skip_space();
                        if (pos < text.size() && text[pos] == L'\r') ++pos;
                        if (pos < text.size() && text[pos] == L'\n') ++pos;
                    }
                    pos = scan(pos, false);
                    skip_space();
                    find_line();
                    if (pos < line_end) continue;
                }
            }
            else if (const size_t equals = find(L'='); equals != std::wstring::npos) {
                if (top) options.push_back({pos, equals, code_end});
            }
            if (!next_line()) break;
            skip_space();
            find_line();
        }
        return pos;
    }
};

std::string last_error(const char *operation, DWORD code) {
    return utils::format("%s (Windows error %lu)", operation, code);
}

}  // namespace

const char *name(Skybox skybox) {
    switch (skybox) {
        case Skybox::Off: return "Off";
        case Skybox::Low: return "Low";
        case Skybox::High: return "High";
    }
    return "High";
}

const char *name(DamageAttribution attribution) {
    return attribution == DamageAttribution::Original ? "Original" : "Corrected";
}

const char *name(GroupCountMode mode) {
    return mode == GroupCountMode::Original ? "Original" : "Corrected";
}

const char *name(PlaceContainmentMode mode) {
    return mode == PlaceContainmentMode::Original ? "Original" : "Corrected";
}

const char *name(ZeroXTraceMode mode) {
    return mode == ZeroXTraceMode::Original ? "Original" : "Corrected";
}

Selection resolve(const Base::CBlockPar &options) {
    Selection selection;
    auto &values = selection.values;
    // Display and random keys keep their established all-or-default contracts and diagnostics.
    values.display = Display::resolve(options).settings;
    values.random = RandomConfiguration::resolve(options).mode;
    read(options, L"SoundVolume", values.sound_volume,
         [](const std::wstring &value) { return percent(value, 0, 100); }, selection);
    read(options, L"CameraSpeed", values.camera_speed,
         [](const std::wstring &value) { return percent(value, min_camera_speed, max_camera_speed); }, selection);
    read(options, L"StencilShadows", values.stencil_shadows, boolean, selection);
    read(options, L"ProjectedShadows", values.projected_shadows, boolean, selection);
    read(options, L"Skybox", values.skybox, skybox, selection);
    read(options, L"RobotShadows", values.robot_shadows, boolean, selection);
    read(options, L"DelayedDamage", values.delayed_damage, delayed_damage, selection);
    if (options.ParCount(L"Map")) {
        if (const auto map = map_path(options)) values.map = *map;
        else selection.invalid.push_back("Map");
    }
    read(options, L"BattleBonus", values.battle_bonus, battle_bonus, selection);
    read(options, L"GroupCounts", values.group_counts, group_counts, selection);
    read(options, L"PlaceContainment", values.place_containment, place_containment, selection);
    read(options, L"ZeroXTrace", values.zero_x_trace, zero_x_trace, selection);
    read(options, L"MusicVolume", values.music_volume,
         [](const std::wstring &value) { return percent(value, 0, 100); }, selection);
    read(options, L"Mods", values.mods, mod_list, selection);
    read(options, L"PersonalMods", values.personal_mods, mod_list, selection);
    read(options, L"FarViewScale", values.far_view_scale,
         [](const std::wstring &value) { return percent(value, 150, 300); }, selection);
    return selection;
}

Selection load(const wchar_t *path) {
    try {
        Base::CBlockPar options;
        if (std::filesystem::exists(path)) options.LoadFromTextFile(path);
        return resolve(options);
    }
    catch (const Base::CException &) {
    }
    catch (const std::exception &) {
    }
    Selection selection;
    selection.invalid.push_back("CFG/standalone.txt");
    return selection;
}

std::wstring update_text(const std::wstring &text, const Values &values) {
    const std::wstring newline = text.find(L"\r\n") != std::wstring::npos || text.find(L'\n') == std::wstring::npos
                                         ? L"\r\n"
                                         : L"\n";
    const Scanner scanner(text);
    std::array<bool, keys.size()> written{};
    std::wstring result;
    size_t copied = 0;
    const auto copy_to = [&](size_t position) {
        result.append(text, copied, position - copied);
        copied = position;
    };
    const auto starts_line = [&](size_t position) {
        return std::all_of(text.begin() + line_start(text, position), text.begin() + position, space);
    };
    for (const auto &option : scanner.options) {
        const std::wstring key = utils::trim(text.substr(option.begin, option.equals - option.begin));
        const auto found = std::ranges::find(keys, key);
        if (found == keys.end()) continue;
        const auto index = static_cast<size_t>(std::distance(keys.begin(), found));
        if (written[index]) {
            // CBlockPar rejects repeated keys; keep the first. A repeat that starts its line is
            // removed with the line, one after a block only up to its comment.
            if (starts_line(option.begin)) {
                copy_to(line_start(text, option.begin));
                const size_t end = text.find(L'\n', option.end);
                copied = end == std::wstring::npos ? text.size() : end + 1;
            }
            else {
                size_t begin = option.begin;
                while (space(text[begin - 1])) --begin;
                copy_to(begin);
                copied = option.end;
            }
            continue;
        }
        written[index] = true;
        size_t value_begin = option.equals + 1;
        while (value_begin < option.end && space(text[value_begin])) ++value_begin;
        size_t value_end = option.end;
        while (value_end > value_begin && space(text[value_end - 1])) --value_end;
        copy_to(value_begin);
        result += value_text(values, key);
        copied = value_end;
    }
    std::wstring missing;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!written[i]) missing += std::wstring(keys[i]) + L"=" + value_text(values, keys[i]) + newline;
    }
    if (scanner.stop < text.size()) {
        // The reader stops at a top-level closing brace, so new keys go before it.
        if (starts_line(scanner.stop)) {
            copy_to(line_start(text, scanner.stop));
        }
        else {
            copy_to(scanner.stop);
            missing.insert(0, newline);
        }
        result += missing;
        copy_to(text.size());
        return result;
    }
    copy_to(text.size());
    if (!result.empty() && result.back() != L'\n') result += newline;
    return result + missing;
}

bool save(const Values &values, std::string &error, const wchar_t *path) {
    const std::filesystem::path target(path);
    auto temporary = target;
    temporary += L".tmp";
    try {
        std::string bytes;
        if (std::filesystem::exists(target)) {
            std::ifstream input(target, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            if (!input.good() && !input.eof()) throw std::runtime_error("Cannot read the current settings file");
        }
        // Match CBlockPar: an even-sized file with a UTF-16LE BOM is wide text, anything else is
        // UTF-8. Supported keys and values are ASCII, so other bytes pass through unchanged.
        const bool wide = bytes.size() >= 2 && bytes.size() % 2 == 0 && bytes[0] == '\xff' && bytes[1] == '\xfe';
        std::wstring text;
        if (wide) {
            for (size_t i = 2; i + 1 < bytes.size(); i += 2)
                text += static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) |
                                             (static_cast<unsigned char>(bytes[i + 1]) << 8));
        }
        else {
            for (char ch : bytes) text += static_cast<wchar_t>(static_cast<unsigned char>(ch));
        }
        const std::wstring updated = update_text(text, values);
        std::string output = wide ? std::string("\xff\xfe") : std::string{};
        for (wchar_t ch : updated) {
            output += static_cast<char>(ch & 0xff);
            if (wide) output += static_cast<char>((ch >> 8) & 0xff);
        }

        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(output.data(), static_cast<std::streamsize>(output.size()));
            stream.flush();
            if (!stream.good()) throw std::runtime_error("Cannot write a temporary settings file");
        }
        const DWORD started = GetTickCount();
        constexpr DWORD retry_limit_ms = 500;
        for (;;) {
            if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
            const DWORD code = GetLastError();
            const DWORD elapsed = GetTickCount() - started;
            // A read-only destination cannot become replaceable by releasing a scanner's handle.
            const DWORD attributes = code == ERROR_ACCESS_DENIED ? GetFileAttributesW(target.c_str()) : INVALID_FILE_ATTRIBUTES;
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY))
                throw std::runtime_error(last_error("Cannot replace the settings file", code));
            if ((code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION) || elapsed >= retry_limit_ms)
                throw std::runtime_error(last_error("Cannot replace the settings file", code));
            // Indexers and scanners can briefly deny replacement after a write.
            // Retry only that boundary; the original file and prepared bytes stay intact.
            Sleep(std::min<DWORD>(10, retry_limit_ms - elapsed));
        }
        return true;
    }
    catch (const std::exception &failure) {
        error = failure.what();
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return false;
}

}  // namespace StandaloneSettings
