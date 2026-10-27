// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <array>
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
// the host; the AI plays it on. Giving up and then leaving is both.
void set_gone(int side, int how);
// The person who lost the connection is back (Commands::Type::Rejoin).
void set_back(int side);

// Tab went down or up. Not while the game's own statistics window is shown (the battle's end).
void hold(bool down);
// Every frame of the battle: refreshes the table twice a second while it is shown.
void update();
// Releases the table (before the battle's interface goes).
void clear();

// A person's side that lost: Defeated + watching, left, lost the connection or removed; an AI's: Defeated.
enum class Status { Fighting, GaveUpWatching, GaveUpLeft, GaveUpLost, GaveUpRemoved, Left, Lost, Removed, Defeated,
                    DefeatedWatching, DefeatedLeft, DefeatedLost, DefeatedRemoved };
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
std::wstring fill_template(std::wstring templ, const std::vector<Row> &rows, bool russian);

}  // namespace BattleStats
