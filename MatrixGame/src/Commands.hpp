// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <functional>
#include <string>
#include <vector>

class CMatrixMapStatic;
struct SSpecialBot;

// Player commands: everything a person does to the battle, as data that does not
// depend on the camera, the window or the interface state. The interface issues
// commands; the simulation executes them at the start of a step. Replays and
// network peers feed the same commands, so every peer runs the same battle.
namespace Commands {

enum class Type : int {
    Stop = 1,
    Move,
    Attack,
    Capture,
    Patrol,
    Bomb,
    Repair,
    AutoAttack,
    AutoCapture,
    AutoDefence,
    BuildRobot,
    BuildTurret,
    CancelBuild,
    Maintenance,
    Surrender,  // params: 0 the person gave up, 1 left the battle, 2 lost the connection; the side plays on under the AI
    // Direct control of the robot `target` (a person "in" it): params action (DriveAction), held keys
    // (DRIVE_* bits), the aim's z, then (a shared battle's) the aim's rate per ms in x and y (float bits);
    // x, y: the aim on the ground.
    Drive,
    // The side's person came back after losing the connection (NetBattle): people command it again.
    Rejoin,
};

enum class DriveAction : int { Input = 0, Enter = 1, Leave = 2, Explode = 3 };
// AIMING: the cursor is over the battle, not the interface (the hull follows the aim).
constexpr int DRIVE_FORWARD = 1, DRIVE_BACKWARD = 2, DRIVE_LEFT = 4, DRIVE_RIGHT = 8, DRIVE_FIRE = 16, DRIVE_AIMING = 32;

// Units value meaning "every live robot and flyer of the side" (scripts and bots).
constexpr int ALL_UNITS = -1;

struct Command {
    int time = -1;           // game time (ms) of execution; -1 until scheduled
    int side = 0;
    Type type = Type::Stop;
    float x = 0, y = 0;      // world point of the order
    int target = 0;          // sim id of the target object; 0: none
    std::vector<int> units;  // sim ids of the ordered robots and flyers
    std::vector<int> params; // type-specific, see Commands.cpp
};

// Deferred: issued commands wait for the next simulation step (lab, network).
// Otherwise they run at once, as the classic single-player game did.
void set_deferred(bool deferred);
bool deferred();

// From the interface of this client's side.
void issue(Command command);
// Network battles: issued commands go to the peers (Lockstep), which schedule them; null: off.
void set_router(std::function<void(Command)> router);
// At a fixed game time (replays, scripts, peers).
void schedule(Command command);
// Runs every command due at the current game time; called at the start of a step.
void execute_due();
// Receives each command right before it is executed, with its time set.
void set_observer(std::function<void(const Command &)> observer);
// Forgets queued commands, the observer and the router (battle end).
void reset();
// True while the interface may change the battle only through commands.
bool interface_locked();
// False while the battle's commands come from elsewhere only (a lab command replay, a lab run without
// people): the interface then issues none, such as a robot's keys. reset() makes it true.
void set_interface_input(bool live);
bool interface_input();

std::string to_line(const Command &command);
bool from_line(const std::string &line, Command &command);
const char *type_name(Type type);

// Live logic object with this sim id, or nullptr.
CMatrixMapStatic *find(int sim_id);

// BuildRobot params: count, team, chassis, armor, head, then kind and mount of each weapon.
std::vector<int> design_params(const SSpecialBot &design, int count, int team);
bool design_from_params(const std::vector<int> &params, SSpecialBot &design, int &count, int &team);

}  // namespace Commands
