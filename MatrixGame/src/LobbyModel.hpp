// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Lobby.hpp"
#include "SettingsModel.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Device-independent text and geometry of the network battle screens: the lobby (a paged list of the
// installed maps, the selected map's preview, the people and their sides) and the screen where a
// person types the host's address.
namespace LobbyModel {

using SettingsModel::Language;

// The lobby is laid out on this logical grid and drawn `scale` times larger (Screen::scale): it fills the
// window, its fonts sized with it (the menu's Verdana, heavier: Font.Heavy<px>). Hits are in the grid's units.
struct Layout {
    static constexpr int width = 1070;
    static constexpr int content_height = 600;
    static constexpr int title_y = 24;
    static constexpr int list_x = 40;
    static constexpr int list_width = 300;
    static constexpr int capacity_width = 30;  // the map's people, right of its name
    static constexpr int header_y = 52;   // "Map" and "People" over the list
    static constexpr int first_row_y = 76;
    static constexpr int row_height = 22;
    static constexpr int visible_rows = 18;
    static constexpr int pager_y = 476;
    static constexpr int pager_width = 24;
    static constexpr int preview_x = 365;
    static constexpr int preview_y = 76;
    static constexpr int preview_width = 245;
    static constexpr int preview_height = 220;
    static constexpr int caption_y = 302;  // the map's whole name, in two lines when it needs them
    static constexpr int map_info_y = 346;
    // Under the preview: the host's switches of direct robot control and its bonus, and what the bonus is.
    static constexpr int manual_y = 380;
    static constexpr int manual_bonus_y = 402;
    static constexpr int manual_note_y = 424;
    static constexpr int dropped_y = 470;  // the host's switch: a dropped person's side to the AI or without orders
    static constexpr int people_x = 640;
    static constexpr int people_width = 390;
    static constexpr int people_title_y = 52;
    static constexpr int people_y = 76;
    static constexpr int person_height = 24;
    // People columns from people_x: name, side (this person's has < > to change it), bonus, ping, ready.
    static constexpr int side_column = 110;
    static constexpr int side_width = 112;
    static constexpr int side_arrow = 18;
    static constexpr int bonus_column = 226;
    static constexpr int bonus_width = 64;
    static constexpr int ping_column = 292;
    static constexpr int ping_width = 52;
    static constexpr int ready_column = 344;
    static constexpr int bonus_title_y = 192;
    static constexpr int bonus_y = 214;
    static constexpr int ai_y = 298;
    static constexpr int delay_y = 322;   // the battle's delay: the host's < >
    static constexpr int address_y = 358;
    static constexpr int status_y = 508;
    static constexpr int legend_y = 532;
    static constexpr int buttons_y = 566;
    static constexpr int mods_x = 205;  // shows the mods in the list's place, and back to the maps
    static constexpr int primary_x = 455;
    static constexpr int leave_x = 705;
    static constexpr int button_width = 160;
    static constexpr int mod_rows = 3;  // a mod in the list: its name, then two lines of its description
    static constexpr int toggle_size = 16;  // a mod's switch, right of its name: a round button, lit when on
};

// A length of the grid in pixels at this scale.
int px(double scale, int value);
// The lobby's scale in a window of this size: as large as fits, never smaller than the grid.
double scale_for(int screen_width, int screen_height);

struct MapRow {
    std::wstring name;   // shown to people
    int capacity = -1;   // people the map takes (its playable sides); -1 not read yet, 0 unreadable
};

// The map list's order, chosen by its column headers.
enum class MapSort { Name, People };

// A battle mod in the lobby's list: the host's installed ones; a client's, with those the host chose that it lacks.
struct ModRow {
    std::wstring name, description;
    bool on = false;  // the host chose it for the battle
    enum class Problem { None, Missing, Differs } problem = Problem::None;  // a client's copy of a mod that is on
};

// Separator lines between the lobby's parts (the game's statistics lines, stretched).
struct Line {
    int x, y, length;
    bool vertical;
};
const std::vector<Line> &separators();
std::wstring line_bitmap(int index);  // the runtime bitmap name of separators()[index]
std::wstring toggle_bitmap(bool on);   // the runtime bitmap name of a mod's switch (Layout::toggle_size square)

// Why this PC cannot play the lobby's map.
struct MapProblem {
    enum class Kind { None, PackMissing, PackDiffers, FilesMissing };
    Kind kind = Kind::None;
    std::wstring what;  // PackMissing: the map mod's id; PackDiffers: its name; FilesMissing: the first file lacking
    int count = 0;      // FilesMissing: how many files lack (as far as counted)
    bool operator==(const MapProblem &) const = default;
};

struct Screen {
    Language language = Language::English;
    bool host = true;
    bool local = false;        // the lobby without a network: the host alone, the AI plays the other sides
    std::vector<MapRow> maps;
    MapSort sort = MapSort::Name;
    std::vector<int> order;    // the list's rows as indexes into `maps`; empty: in the order of `maps`
    int selected = -1;         // the lobby's map in `maps`; -1 none
    std::wstring missing_map;  // a client's: the host's map that this PC does not have
    bool map_differs = false;  // a client's: this PC's file of the host's map is another one
    int map_progress = -1;     // a client's: percent of the host's map file received while it comes (MapShare)
    bool map_failed = false;   // a client's: the host's map file could not be received
    MapProblem map_problem{};  // the map's map mod or files, on this PC
    int page = 0;
    std::vector<Lobby::Person> people;
    int you = 0;
    int ai_resources = 0;
    bool manual_control = true;
    bool manual_bonus = true;
    SettingsModel::Resolution preview{0, 0};
    std::wstring address;      // host: where others reach it; client: the host
    bool loading = false;      // a client's: the host is loading the battle
    int kick_asked = -1;       // the host's: the person whose row asks whether to send them away
    int delay = 0;             // Lobby::State::delay: ms, 0 auto
    int auto_delay = 0;        // Lobby::State::auto_delay: ms, 0 measuring
    bool dropped_ai = true;    // Lobby::State::dropped_ai
    std::vector<ModRow> mods;
    bool mods_view = false;    // the list shows the mods
    int mods_page = 0;
    double scale = 1;          // pixels per unit of Layout
    // The drawn width of a text in a font; empty (tests): texts are not shortened.
    std::function<int(const std::wstring &, const wchar_t *font)> text_width;
};

// The text as it fits in one line of `width` pixels: cut with "..." when longer (a hint text wraps,
// and the second line falls under the next row).
std::wstring fit(const Screen &screen, const std::wstring &text, const wchar_t *font, int width);
// A list row: in Font.2Normal when it fits, else in the smaller Font.2Small, cut only when even that is
// too long.
struct Fitted {
    std::wstring text;
    const wchar_t *font;
};
Fitted fit_row(const Screen &screen, const std::wstring &text, int width);

// A lobby is for two people at least: a map is offered when it takes everyone in, and two at least.
bool offered(const Screen &screen, int index);
// The host starts once everyone is ready; the lobby without a network, at once.
bool can_start(const Screen &screen);
// A client has every mod the host chose, the same as the host's.
bool mods_fine(const Screen &screen);
// This PC has the map's map mod (the host's) and every file the map names.
bool map_fine(const Screen &screen);
int mods_page_count(const Screen &screen);
// The mods' page `delta` pages on from the one shown, within the list.
int mods_page_moved(const Screen &screen, int delta);
// Two people or more chose one side: the start draws who keeps it.
bool sides_coincide(const Screen &screen);
int page_count(const Screen &screen);
int page_of(int row);
// Rows in this order: names alphabetically (case and Cyrillic Yo aside), or the people a map takes, fewest
// first and by name among equals (maps not read yet or unreadable last).
std::vector<int> map_order(const std::vector<MapRow> &maps, MapSort sort);
int row_of(const Screen &screen, int index);  // the list row showing maps[index]; -1 none

struct Hit {
    int map = -1;
    int page = 0;    // -1 or +1: a pager arrow
    int bonus = -1;
    int ai = 0;      // -1 or +1: an AI economy arrow
    int manual = 0;  // 1 the switch of direct robot control, 2 of its bonus, 3 who plays a dropped person's side
    int side = 0;    // -1 or +1: an arrow of this person's side
    int sort = -1;   // a list header: the order it chooses (MapSort)
    int action = 0;  // 1 the primary button (start, ready), -1 leave, 2 the mods' button
    int mod = -1;    // the mods' list: a mod's rows (index into Screen::mods)
    int person = -1; // the host: a client's name (asks whether to send them away)
    int kick = 0;    // the host, in the asking row: 1 send away, -1 not
    int delay = 0;   // -1 or +1: a delay arrow
};
Hit hit(const Screen &screen, int x, int y);

std::wstring status(const Screen &screen);
bool status_is_error(const Screen &screen);
std::wstring hint_template(const Screen &screen);

// The screen where a person types the host's address.
struct JoinLayout {
    static constexpr int width = 560;
    static constexpr int content_height = 230;
    static constexpr int field_y = 95;
    static constexpr int status_y = 150;
    static constexpr int buttons_y = 185;
    static constexpr int connect_x = 120;
    static constexpr int back_x = 300;
    static constexpr int button_width = 140;
};

enum class JoinError { None, BadAddress, Unreachable, Full, Version, Build, Data, Name, Started, HostLeft, Lost, Busy, Kicked };

struct JoinScreen {
    Language language = Language::English;
    std::wstring address;
    bool connecting = false;
    JoinError error = JoinError::None;
    double scale = 1;  // drawn at the lobby's scale (scale_for); hits are in JoinLayout's units
};
std::wstring join_error_text(Language language, JoinError error);
// 1 connect, -1 back, 0 nothing.
int join_hit(int x, int y);
std::wstring join_template(const JoinScreen &screen);

// The screen where a person chooses the name others see (JoinLayout's geometry). forced: the first
// start, when there is no name yet; it cannot be left without one.
struct NameScreen {
    Language language = Language::English;
    std::wstring name;
    bool forced = false;
    double scale = 1;  // as JoinScreen's
};
// 1 save, -1 cancel, 0 nothing.
int name_hit(const NameScreen &screen, int x, int y);
std::wstring name_template(const NameScreen &screen);

// "a.b.c.d" or "a.b.c.d:port"; a missing port is Lobby::default_port.
struct Endpoint {
    std::string address;
    int port = Lobby::default_port;
};
std::optional<Endpoint> parse_endpoint(const std::wstring &text);
// Whether a typed character may go into the address.
bool address_char(wchar_t ch);
constexpr std::size_t address_length = 21;

}  // namespace LobbyModel
