// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

// The battle's statistics while Tab is held: the game's own statistics table of the battle's end
// (template Stat) with a column of who plays each side (a person's name or the AI) and each side's
// status instead of its time.
namespace BattleStats {

constexpr int side_count = 4;

// The people of a network battle by side ([0] side 1); empty: nobody or no name. Without them (a battle
// alone) the player's side shows this PC's PlayerName.
void set_names(const std::array<std::wstring, side_count> &names);
void clear_names();
// A person stopped playing a side: 0 gave up (and watches on), 1 left, 2 lost the connection, 3 removed by
// the host, 4 kicked out for good; the AI plays it on. Giving up and then leaving is both.
void set_gone(int side, int how);
// The person who lost the connection is back (Commands::Type::Rejoin).
void set_back(int side);

// Tab went down or up. Not while the game's own statistics window is shown (the battle's end).
void hold(bool down);
// Every frame of the battle: refreshes the table twice a second while it is shown.
void update();
// Releases the table (before the battle's interface goes).
void clear();

// The host of a shared battle: a Kick button right of the table's row of each person it may put out of the battle for good
// (in it, or gone and free to come back). The first click on one asks again; the second kicks.
struct Kickable {
    int peer = -1;  // the host's connection to them
    int side = 0;
    std::wstring name;  // empty: the faction alone
    bool kicked = false;  // already: the button grey, not pressable
    bool operator==(const Kickable &) const = default;
};
void set_kickable(std::vector<Kickable> people);
bool kick_shown();
void kick_hover(int x, int y);
// A click at (x, y): the person's peer once the click confirms a kick; -2 a button asked again; -1 no button there.
int kick_click(int x, int y);
// Screen rectangles of the shown Kick buttons (tests): x, y, width, height each.
std::vector<std::array<int, 4>> kick_rects();

// The Kick column's cell per person, as the table's text: "Kick out", the armed one asking again, `hover` the one
// under the mouse (-1: none), a kicked person's grey.
std::vector<std::wstring> kick_cells(const std::vector<Kickable> &people, int armed, int hover, bool russian);

// A person's side that lost: Defeated + watching, left, lost the connection or removed; an AI's: Defeated.
enum class Status { Fighting, GaveUpWatching, GaveUpLeft, GaveUpLost, GaveUpRemoved, Left, Lost, Removed, Defeated,
                    DefeatedWatching, DefeatedLeft, DefeatedLost, DefeatedRemoved, Kicked, GaveUpKicked, DefeatedKicked };
struct Row {
    std::wstring key;     // the side's row of the template (CMatrixMap::StatisticsKey: _p_, _r_, ...)
    int side = 0;
    std::wstring player;  // a name or "AI"
    bool you = false;
    Status status = Status::Fighting;
    std::array<int, 5> numbers{};  // robots built, robots killed, turrets built, turrets killed, buildings killed
};
// The game's Stat template filled for the rows: the player column, the numbers, the status column and
// no OK button. Rows of the template no side has are left empty (the template hides them).
// `kick`: the host's Kick column, a cell per row key (_p_, _r_, _g_, _b_; empty: no button, the cell's place marked
// otherwise); none when empty.
std::wstring fill_template(std::wstring templ, const std::vector<Row> &rows, bool russian, const std::vector<std::wstring> &kick = {});

}  // namespace BattleStats
