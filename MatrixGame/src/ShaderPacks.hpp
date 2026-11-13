// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Mods.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Shader packs: the shaders folder beside the EXE, a pack per folder or .zip with shaders.txt at its top (or in its
// only folder). A pack is HLSL (Shader Model 3) passes drawn over the battle's 3D picture before the interface, each
// into a target of its own or the screen; its author's Setting. lines become choices on the settings' Shaders page
// and #defines in its code. Drawing only: a battle never reads what they draw (docs/SHADERS.md).
namespace ShaderPacks {

inline constexpr const wchar_t *folder = L"shaders";
inline constexpr const wchar_t *description_file = L"shaders.txt";
inline constexpr const wchar_t *choice_file = L"CFG\\shaders.txt";  // this person's: on, the pack, its settings

// What a pack may use. Shader Model 3 gives a pass 16 samplers: the scene, its depth, the previous pass and SolidTex
// are the engine's, the other 12 are the pack's targets and textures that pass's code names (each pass its own 12).
inline constexpr int max_passes = 100;
inline constexpr int max_targets = 16;
inline constexpr int max_textures = 8;
inline constexpr int max_pass_pictures = 12;  // the targets and textures one pass reads (the shadow map: one of them)
inline constexpr std::size_t max_file = 8u << 20;     // a file of a pack
inline constexpr std::size_t max_total = 64u << 20;   // all of a pack's files
inline constexpr int max_files = 2000;

enum class Format { RGBA8, RGBA16F, RG16F, R32F };

struct Target {
    std::string name;  // the sampler's name in the code
    float scale = 1.0f;  // of the screen: 1, 0.5, 0.25...
    Format format = Format::RGBA8;
    int width = 0, height = 0;  // in pixels instead (Target.<name>=<width>x<height>): 0, the scale's
    bool operator==(const Target &) const = default;
};

struct Pass {
    std::wstring file;   // in the pack, '/' between folders
    std::string output;  // a target's name or "screen" (the last pass's must be)
    std::string entry = "main";
    bool operator==(const Pass &) const = default;
};

struct Texture {
    std::string name;  // the sampler's name in the code
    std::wstring file;  // .png .dds .bmp .tga .jpg
    bool clamp = false, point = false;
    bool operator==(const Texture &) const = default;
};

// A row of a pack's settings screen: a setting's (its < value >), a further screen's (opens it), a note's (a line with
// its "i"), an empty one, the profiles' (its < value > goes through the pack's profiles).
struct Item {
    enum class Kind { Setting, Screen, Note, Empty, Profile };
    Kind kind = Kind::Empty;
    int index = -1;  // in the pack's settings, screens or notes
    bool operator==(const Item &) const = default;
};

// A settings screen: the first is the pack's own (Screen=, or all its settings in order), the others are opened from a
// row (Screen.<Id>=<Russian>|<English>|<rows>).
struct Screen {
    std::string id;  // empty: the first
    std::wstring russian, english;
    std::vector<Item> items;
    bool operator==(const Screen &) const = default;
};

// A line of text on a screen (Note.<Id>=<Russian>|<English>); what its "i" says is its Info.
struct Note {
    std::string id;
    std::wstring russian, english;
    bool operator==(const Note &) const = default;
};

// Several settings set at once (Profile.<Id>=<Russian>|<English>|<Setting>=<value> ...: Low, High...), chosen on the
// screens' PROFILE row; settings it does not name stay as they are.
struct Profile {
    std::string id;
    std::wstring russian, english;
    std::vector<std::pair<std::string, std::wstring>> values;  // a setting's id and its value
    bool operator==(const Profile &) const = default;
};
inline constexpr const char *profile_row = "PROFILE";  // the row's name in Screen= lines, its Info's id

// The shadow map (Shadow=<size>|<distance>[|<setting>]): the world's depth as the map light sees it, drawn each frame
// before the picture (the world is drawn twice: only a pack that asks pays for it). `size`: its pixels a side;
// `distance`: how far from the camera it reaches; `when`: a setting's id, drawn only while that one is not 0 or Off.
struct Shadow {
    int size = 0;  // 0: the pack asks for none
    float distance = 0;
    std::string when;
    bool operator==(const Shadow &) const = default;
};

// The world pass (World=<file>|<targets>[|<setting>]): the pack's own pixel shader draws the world's solid things (the
// land, the map's objects, robots, buildings, turrets, flyers) into 1 to 4 of its targets (of the screen's size and
// one format), before the game draws its picture: real normals, the surface's texture, what kind of thing it is.
// The world is drawn once more for it. `when`: a setting's id, drawn only while that one is not 0 or Off.
struct WorldPass {
    std::wstring file;  // empty: the pack has none
    std::vector<std::string> outputs;
    std::string when;
    bool operator==(const WorldPass &) const = default;
};
inline constexpr int max_world_outputs = 4;

// The voxel copy (Voxels=<a voxel's size>[|<setting>]): the whole map as a grid of cubes of that many of the world's
// units (4 to 64; larger on a map too big for 512 of them a side), each holding what stands there: the land and what
// is built on it or grows there (not robots or flyers: they move). Built once a map over its first frames, kept up
// as things are built and destroyed. A pass reads it by VoxelAt (a 3D texture, VoxelTex).
struct Voxels {
    float size = 0;  // 0: the pack asks for none
    std::string when;
    bool operator==(const Voxels &) const = default;
};
inline constexpr const char *world_entry = "pbo_world";  // world_wrapper's, which calls the pack's main
// What a thing drawn by the world pass is (World.material, the code's MAT_ names).
enum class Material { None = 0, Land = 1, Object = 2, Robot = 3, Building = 4, Cannon = 5, Flyer = 6, Water = 7 };

// The water (Water=<file>[|<setting>]) and the effects (Effects=<file>[|<setting>]) drawn by the pack's own pixel
// shader in the game's place: its main gets what the game had (its colour, textures, light, the place) and more
// (the picture and the depth under the water; the depth before the effects), and returns the colour the game then
// lays as it does. `when`: a setting's id, drawn so only while that one is not 0 or Off.
struct Shaded {
    std::wstring file;  // empty: the game's own drawing
    std::string when;
    bool operator==(const Shaded &) const = default;
};
inline constexpr const char *water_entry = "pbo_water";    // water_wrapper's
inline constexpr const char *effects_entry = "pbo_effect";  // effects_wrapper's

// A texture stage of the game's drawing as Direct3D 9 has it (D3DTOP_ and D3DTA_ values): the effects are drawn so,
// and their shader works out the same first (Effect.color).
struct Stage {
    unsigned color_op = 1, color1 = 2, color2 = 1;  // D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT
    unsigned alpha_op = 1, alpha1 = 2, alpha2 = 1;
    bool cube = false;  // its texture a cube map (read by three coordinates)
    bool operator==(const Stage &) const = default;
};
inline constexpr int effect_stages = 3;
using Stages = std::array<Stage, effect_stages>;
// The code that works out what the stages make of a pixel into `cur` (from i.diffuse, i.specular, the stages'
// textures t0..t2 and PBO_TFactor); how many stages it reads (the first ones). nullopt: a stage does what it cannot
// (that draw stays the game's).
struct Combiner {
    std::string code;
    int stages = 0;
};
std::optional<Combiner> combiner(const Stages &stages);

// What a setting's or a note's "i" says (Info.<Id>=<Russian>|<English>, a line each).
struct Comment {
    std::wstring russian, english;
    bool operator==(const Comment &) const = default;
};

struct Pack {
    std::wstring id;  // its folder's or file's name in shaders (the .zip kept): what CFG\shaders.txt names
    std::wstring name, name_en, description, description_en, author;
    std::vector<Mods::SettingLine> settings;
    std::vector<Screen> screens;  // at least the first (when the pack has no problem)
    std::vector<Note> notes;
    std::vector<Profile> profiles;
    std::map<std::string, Comment> comments;  // by a setting's or a note's id (the profiles' row: profile_row)
    std::vector<Target> targets;
    std::vector<Pass> passes;
    std::vector<Texture> textures;
    Shadow shadow;
    WorldPass world;
    Voxels voxels;
    Shaded water, effects;
    // The game's own settings it decides while it draws (Game.<Name>=On|Off): game_settings names them. Their rows
    // in the settings are grey with its value.
    std::map<std::string, bool> game;
    std::wstring problem;  // why it cannot be used (empty: it can)
    std::uint64_t stamp = 0;  // of its files' names and bytes (scan): a pack changed on disk is built anew
    bool operator==(const Pack &) const = default;
};

// shaders.txt (UTF-8, '#' comments): Name=, NameEn=, Description= (each a line), DescriptionEn=, Author=,
// Setting.<Id>=<values>|<default>|<Russian>|<English> (as a mod's), Target.<name>=<scale or WxH>|<format>,
// Texture.<name>=<file>|<clamp/wrap>|<linear/point>, Pass=<file>|<target or screen>[|<entry>]; the settings' screens:
// Screen=<rows>, Screen.<Id>=<Russian>|<English>|<rows> (rows by spaces: a setting's or a note's id, [<screen>],
// - an empty one, PROFILE the profiles'), Note.<Id>=<Russian>|<English>, Info.<Id>=<Russian>|<English>,
// Profile.<Id>=<Russian>|<English>|<Setting>=<value> ...; Game.<Name>=On|Off (game_settings);
// Shadow=<size>|<distance>[|<setting>] (the shadow map); World=<file>|<targets by spaces>[|<setting>] (the world
// pass); Voxels=<size>[|<setting>] (the voxel copy); Water=<file>[|<setting>], Effects=<file>[|<setting>].
// problem says what is wrong with it.
Pack parse(std::wstring_view text, std::wstring id);

// What a pack may decide of the game's own drawing: Fog (the game's distance fog: off for a pack that makes its own),
// StencilShadows, ProjectedShadows (the settings' rows of those names).
inline constexpr const char *game_settings[] = {"Fog", "StencilShadows", "ProjectedShadows"};

// A pack's files by their path inside it ('/' between folders, lower case).
using Files = std::map<std::wstring, std::string>;
// The files of a zip (stored or deflated, not encrypted, not zip64), within the limits above; nullopt with `error`.
std::optional<Files> read_zip(std::string_view bytes, std::string &error);
// A pack's files: its folder's or its zip's (shaders.txt's folder becomes the top). nullopt with `error`.
std::optional<Files> load_files(const std::filesystem::path &place, std::string &error);
// The packs in a folder by name; a folder or zip without shaders.txt is not a pack (left out).
std::vector<Pack> scan(const std::filesystem::path &shaders);
// shaders beside the EXE, read the first time; rescan reads them again (the drawing builds its pack anew only when
// something of them changed).
const std::vector<Pack> &installed();
void rescan();
inline const Pack *find(const std::vector<Pack> &packs, std::wstring_view id) {
    for (const auto &pack : packs)
        if (pack.id == id) return &pack;
    return nullptr;
}

// A file of the pack as a pass or #include names it (relative to the pack's top, no "..").
std::optional<std::string> file(const Files &files, std::wstring_view name);

// The pack's pictures a pass reads: those its code names (its file's and what that includes; a name in a comment
// counts too), the targets then the textures, in the order of their samplers (s3 and on).
struct Inputs {
    std::vector<int> targets;   // in pack.targets
    std::vector<int> textures;  // in pack.textures
    bool shadow = false;        // the shadow map (ShadowTex, ShadowDepth or ShadowLit named), after them
    bool voxels = false;        // the voxel copy (VoxelTex or VoxelAt named), after that
    std::size_t size() const { return targets.size() + textures.size() + (shadow ? 1 : 0) + (voxels ? 1 : 0); }
    bool operator==(const Inputs &) const = default;
};
Inputs pass_inputs(const Pack &pack, const Files &files, const Pass &pass);
// Every picture of the pack (a pack of few: the tests').
Inputs all_inputs(const Pack &pack);

// What the engine puts before a pass's code: the settings' #defines, the samplers of the pictures it reads and the
// constants (docs/SHADERS.md).
std::string prelude(const Pack &pack, const Mods::SettingValues &values, const Inputs &inputs);
inline std::string prelude(const Pack &pack, const Mods::SettingValues &values) {
    return prelude(pack, values, all_inputs(pack));
}
// The same for the world pass's code (the settings, the constants, the World its main gets), and what comes after
// that code: the entry (world_entry) that fills World, calls the pack's main and keeps the game's alpha test.
std::string world_prelude(const Pack &pack, const Mods::SettingValues &values);
std::string world_wrapper(const Pack &pack);
// The water's: its prelude (Water and what it reads), and the entry (water_entry) that fills Water, calls the pack's
// main and lays the game's fog over what it returns.
std::string water_prelude(const Pack &pack, const Mods::SettingValues &values);
std::string water_wrapper();
// The effects': a shader model 2 code (the game's own vertex processing goes on: its texture coordinates, its
// light), the entry (effects_entry) for the stages of one kind of draw.
std::string effects_prelude(const Pack &pack, const Mods::SettingValues &values);
std::string effects_wrapper(const Stages &stages, const Combiner &combine);
// The settings' key in CFG\shaders.txt.
inline std::string setting_key(const Pack &pack, const Mods::SettingLine &setting) {
    std::string key;
    for (wchar_t ch : pack.id) key += ch < 0x80 ? char(ch) : '_';
    return key + "." + setting.id;
}
// A setting's value: the person's when it is one of its values, else its default.
inline std::wstring setting_value(const Pack &pack, const Mods::SettingLine &setting, const Mods::SettingValues &values) {
    const auto found = values.find(setting_key(pack, setting));
    if (found != values.end())
        for (const auto &value : setting.values)
            if (value == found->second) return value;
    return setting.default_value;
}
// Whether the setting a line names (Shadow=...|<setting>, World=...|<setting>) has it on: not 0 or Off (no setting
// named: on).
inline bool setting_on(const Pack &pack, const std::string &when, const Mods::SettingValues &values) {
    if (when.empty()) return true;
    const auto setting = std::ranges::find(pack.settings, when, &Mods::SettingLine::id);
    if (setting == pack.settings.end()) return true;
    const std::wstring value = setting_value(pack, *setting, values);
    return value != L"0" && value != L"Off" && value != L"off" && value != L"OFF";
}
// Whether the pack's shadow map is drawn: it asks for one and its setting has it on.
inline bool shadow_on(const Pack &pack, const Mods::SettingValues &values) {
    return pack.shadow.size > 0 && setting_on(pack, pack.shadow.when, values);
}
// Whether the pack's voxel copy is kept.
inline bool voxels_on(const Pack &pack, const Mods::SettingValues &values) {
    return pack.voxels.size > 0 && setting_on(pack, pack.voxels.when, values);
}
// Whether the pack draws the water, the effects.
inline bool water_on(const Pack &pack, const Mods::SettingValues &values) {
    return !pack.water.file.empty() && setting_on(pack, pack.water.when, values);
}
inline bool effects_on(const Pack &pack, const Mods::SettingValues &values) {
    return !pack.effects.file.empty() && setting_on(pack, pack.effects.when, values);
}
// Whether the pack's world pass is drawn.
inline bool world_on(const Pack &pack, const Mods::SettingValues &values) {
    return !pack.world.file.empty() && setting_on(pack, pack.world.when, values);
}
// The profile the settings are at now (each setting it names has its value): its index, -1 none (the person's own).
inline int profile_now(const Pack &pack, const Mods::SettingValues &values) {
    for (int i = 0; i < int(pack.profiles.size()); ++i) {
        bool same = true;
        for (const auto &[id, value] : pack.profiles[i].values) {
            const auto setting = std::ranges::find(pack.settings, id, &Mods::SettingLine::id);
            same = same && setting != pack.settings.end() && setting_value(pack, *setting, values) == value;
        }
        if (same) return i;
    }
    return -1;
}
// The settings as a profile sets them (the others kept).
inline void set_profile(const Pack &pack, int profile, Mods::SettingValues &values) {
    if (profile < 0 || profile >= int(pack.profiles.size())) return;
    for (const auto &[id, value] : pack.profiles[profile].values)
        if (const auto setting = std::ranges::find(pack.settings, id, &Mods::SettingLine::id); setting != pack.settings.end())
            values[setting_key(pack, *setting)] = value;
}

// The person's choice: shaders on or off, the pack, its settings (CFG\shaders.txt: Shaders.On=, Shaders.Pack=, then
// <pack>.<Id>= lines).
struct Choice {
    bool on = false;
    std::wstring pack;
    Mods::SettingValues settings;
    bool operator==(const Choice &) const = default;
};
Choice load_choice(const std::filesystem::path &path);
// A game setting (game_settings) as the chosen pack decides it: nothing while shaders are off, the pack is missing
// or broken, or it leaves that one to the person.
// (Inline: the settings' tests link no ShaderPacks.cpp.)
inline std::optional<bool> game_setting(const std::vector<Pack> &packs, const Choice &choice, std::string_view name) {
    if (!choice.on) return std::nullopt;
    const Pack *pack = find(packs, choice.pack);
    if (!pack || !pack->problem.empty()) return std::nullopt;
    const auto found = pack->game.find(std::string(name));
    if (found == pack->game.end()) return std::nullopt;
    return found->second;
}
bool save_choice(const std::filesystem::path &path, const Choice &choice, std::string &error);
// The person's now (read from choice_file the first time); set_choice also makes the battle's drawing take it up.
const Choice &choice();
void set_choice(Choice choice);
// Grows by one with each set_choice: the drawing compares it with the one it built.
std::uint32_t choice_version();

}  // namespace ShaderPacks
