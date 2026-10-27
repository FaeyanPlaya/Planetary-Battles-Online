// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "BattleRules.hpp"
#include "Lobby.hpp"
#include "Lockstep.hpp"
#include "NetLink.hpp"
#include "StandaloneSettings.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A battle shared by several game processes: the host chooses the map, seed and rules (each
// person's bonus comes from that person's settings), every peer steps in fixed steps through
// lockstep turns (Lockstep), and the world hashes are compared every second.
//
// Command line: --net-host [address:]port | --net-join [address:]port, and optionally
//   --net-map <map>      host: battle map (bare name or Matrix\Map\... path); default: the settings' map
//   --net-seed <n>       host: random seed; default: the clock
//   --net-players <n>    host: people in the battle, 2..4 (sides 1..n); default 2
//   --net-delay <turns>  host: turns between a command and its execution; default 2 (a lobby: its choice, auto)
//   --net-trace <file>   world hashes and executed commands, for comparing peers
//   --net-bot <file>     commands for this peer's side at their game times ("C ..." lines; tests)
//   --net-exit <ms>      close at this game time (tests)
//   --net-fast           step as fast as the peers allow instead of in real time (tests)
//   --net-test-desync <ms>  draw one extra battle random at this game time here only (tests the
//                        desync report)
//   --net-test-lost <ms>,<length>  behave as with a lost device from this game time for this much game
//                        time (tests that the battle goes on)
//   --net-test-hang <ms>,<length>  stop answering at this game time for this many real milliseconds
//                        (tests the waiting window)
//   --net-test-turret <ms>,<length>  point a new turret at this person's base from this game time for this
//                        much game time, as the base's build buttons do (this PC only), then build it at the
//                        base's first free place as a click does (tests)
//   --net-test-drive <ms>,<length>  enter this person's first robot at this game time and drive it for this
//                        much game time (forward, turning, firing, back) through the keys' commands (tests)
//   --net-manual <control>,<bonus>  host: direct robot control and its bonus, 0 or 1 each; default 1,1
//   --net-rejoin [address:]port  come back to a battle under way after losing the connection (the key from
//                        CFG\rejoin.txt, written when the battle started)
//   --net-rejoin-key <key>  that key instead of the file's (tests)
//   --net-test-crash <ms>  end this process at this game time without a word to the others (tests)
//   --net-dropped ai|idle  host: a dropped person's side is played by the AI (default) or gets no orders
//   --net-test-dump <ms>  write the world's text at this game time next to the trace (tests: finding a desync)
//   --net-test-remove <ms>  host: go on without a person who kept the battle waiting so long, as its button does (tests)
// The address defaults to 127.0.0.1 (this PC only).
//
// The lobby (LobbyWindow) starts the same battles from the startup menu; for tests it opens at once with
//   --lobby-host [address:]port | --lobby-join address:port
//   --lobby-map <map>    host: the map chosen when the lobby opens
//   --lobby-start <n>    host: start once n people are in and every client is ready
//   --lobby-ready        client: ready as soon as it is in
//   --lobby-bonus <name> this person's bonus (None, AirSupport, ReinforcedArmor)
//   --lobby-side <n>     this person's side, chosen once the map's sides are known
//   --lobby-name <name>  this person's name instead of the saved one (several on one PC)
//   --lobby-test-size <w>,<h>  test: the startup menu changes the rendering size first (as Settings would)
//   --lobby-return       the game came back from a finished battle to its lobby: the next battle ends
//                        the same way (not a test)
// and the --net-trace, --net-bot, --net-exit, --net-fast and --net-delay options apply to its battle.
namespace NetBattle {

enum class Role { None, Host, Client };

struct Options {
    Role role = Role::None;
    bool lobby = false;  // --lobby-*: the lobby decides the battle
    std::string address = "127.0.0.1";
    int port = 27960;
    std::wstring lobby_map;
    int lobby_start = 0;
    bool lobby_ready = false;
    std::optional<BattleChoice::Bonus> lobby_bonus;
    int lobby_side = 0;
    std::wstring lobby_name;
    bool lobby_return = false;
    std::pair<int, int> lobby_test_size{0, 0};  // --lobby-test-size
    std::wstring map;
    std::optional<std::uint32_t> seed;
    int players = 2;
    int delay = 0;  // 0: not given (2; a lobby's host chooses, auto by default)
    std::wstring trace;
    std::wstring bot;
    int exit_ms = -1;
    bool fast = false;
    int test_desync_ms = -1;
    int test_lost_ms = -1, test_lost_length = 0;
    int test_hang_ms = -1, test_hang_length = 0;
    int test_turret_ms = -1, test_turret_length = 0;
    int test_drive_ms = -1, test_drive_length = 0;
    int manual_control = 1, manual_bonus = 1;
    int wait_s = 300;  // how long the host waits for people and a client for the host
    bool rejoin = false;  // --net-rejoin
    std::string rejoin_key;
    int test_crash_ms = -1;
    int test_remove_ms = -1;
    int test_dump_ms = -1;
    int dropped_ai = 1;  // --net-dropped: 1 ai, 0 idle
};

// Reads and removes the --net-* arguments. Throws std::runtime_error on a malformed one.
Options parse(std::vector<std::wstring_view> &arguments);

struct Start {
    std::wstring map;  // as CGame::Init takes it
    std::uint32_t seed = 1;
    BattleRules::Rules rules;
    int side = 1;              // this peer's side
    int host_side = 1;
    unsigned human_sides = 0;  // bit per side id
    Lockstep::Config config;
    std::array<std::wstring, BattleRules::side_count> names;  // the people's PlayerNames by side; empty: none
    std::string key;  // a client's: proves it is this side's person when coming back (rejoin); empty: none
};

// A client's way back to its battle (CFG\rejoin.txt, written when the battle starts, forgotten when the
// person leaves it for good). The startup menu offers it.
struct RejoinTicket {
    std::string address;
    int port = 0;
    std::string key;
    int side = 0;
    std::wstring map;
};
std::optional<RejoinTicket> rejoin_ticket();
void forget_rejoin_ticket();
// The startup menu's "Back to the battle": the game starts again with --net-rejoin (restart_to_menu).
void request_rejoin(const std::wstring &name);
// The same for a battle found by joining its host's lobby ("refused started back"): the host knows the person by name.
void request_rejoin_at(const std::string &address, int port, const std::wstring &name);

// The start of a battle chosen in the lobby: person i plays sides[i] (sides[0] the host's).
Start lobby_start(const Lobby::State &state, const std::vector<int> &sides, const StandaloneSettings::Values &settings,
                  std::uint32_t seed, int delay);

// A battle the lobby has started, with its connections (the host's: one per client in the order of
// the clients' sides). The game loads it after the startup menu and runs it like a command-line one.
struct Prepared {
    Options options;
    Start start;
};
void prepare(Options options, Start start, std::vector<std::unique_ptr<NetLink::Connection>> links);
const std::optional<Prepared> &prepared();

// After a battle started from the menu's lobby ends (not by closing the window), the game starts again:
// at the same lobby when the battle was decided and this person stayed to its end (the host opens it
// again on its map, the clients join it), else at the startup menu. True when run asked for that;
// restart_to_menu starts it.
bool return_to_menu();
bool restart_to_menu();

// Before the battle loads. Host: waits for the other people and sends them the map, seed and rules.
// Client: connects to the host and receives them. nullopt: no battle (the reason is logged).
std::optional<Start> connect(const Options &options, const StandaloneSettings::Values &settings);

// After CGame::Init: makes the battle shared, runs the game loop in lockstep and says goodbye.
int run(const Options &options, const Start &start);

// Text of "start" and back (handshake).
std::string start_line(const Start &start);
std::optional<Start> parse_start(std::string_view line);

}  // namespace NetBattle
