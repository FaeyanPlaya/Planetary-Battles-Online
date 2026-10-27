// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "BattleChoice.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The lobby of a battle shared over the network: what its people see and the lines they exchange.
// The host decides the map and the AI economy and sends the whole state on every change; each
// person chooses only their own side, bonus and whether they are ready.
//
// Client to host: hello pbo <version> bonus=<name> data=<fp> build=<fp> name=<nick> [rejoin=<key>] | bonus <name> | ready 0|1 |
//   side <n> | pong <n> | bye
// (rejoin: a person back to the battle under way that lost its connection, with the key its start line gave it.)
// Host to client: lobby you=<index> ... (to_line) | refused version|build|data|full|name|kicked|started |
//   refused rejoin unknown|over|beaten|left | kicked | ping <n> | loading | rules ... | start ... | bye
// (ping: the client answers pong with the same number at once; the host measures the round trip.)
// (data and build: GameData fingerprints of the game files and of the program; both must match.)
// (rules and start are NetBattle's, sent once the host has loaded the map.)
namespace Lobby {

constexpr int protocol_version = 8;
constexpr int max_people = 4;
constexpr int default_port = 27960;

struct Person {
    BattleChoice::Bonus bonus = BattleChoice::Bonus::None;
    bool ready = false;  // the host is ready whenever it starts
    int side = 0;        // the side this person chose; 0: any. Several may choose one side: the start draws.
    std::wstring name;   // PlayerName; empty: none given
    int ping = -1;       // ms, the host's measure of the round trip to this person; -1 unknown (and the host's own)
    bool operator==(const Person &) const = default;
};

struct State {
    std::wstring map;            // Matrix\Map\<file>; empty: none chosen
    int ai_resources = 0;        // BattleRules::Rules::ai_resources
    std::vector<Person> people;  // [0] the host, then the clients in joining order
    std::string map_hash;        // the host's map file (GameData::hash_bytes), to tell a different file
    bool manual_control = true;  // BattleRules::Rules::manual_control and manual_bonus
    bool manual_bonus = true;
    int delay = 0;               // ms between a command and its execution, whole turns; 0: auto (delay_turns)
    bool dropped_ai = true;      // BattleRules::Rules::dropped_ai
    int auto_delay = 0;          // ms: auto's delay, measured after the people last changed (auto_delay_for); 0: measuring
    bool operator==(const State &) const = default;
};

// The battle's delay in turns of turn_ms: the host's choice, or on auto the one measured (auto_delay), or
// while that goes on what the pings shown give (auto_delay_for).
int delay_turns(const State &state, int turn_ms);
// Auto's delay in ms for the slowest round trip: the turn's commands go to the host and back to everyone
// (the round trip), the host gathers them a turn at a time, and 50 ms more for the frames and jitter.
// 30 ms: 200; 85 (a relay): 300; 150: 300; 160: 400; 250: 400. Two turns at least, eight at most.
int auto_delay_for(int slowest, int turn_ms);
// The delay choices the host cycles through: auto, then 100 to 500 ms.
constexpr int delay_choices[] = {0, 200, 300, 400, 500};  // 0: auto; one turn (100) cannot step (Lockstep::usable)
int next_delay(int delay, int step);

// "lobby you=1 ai=0 manual=1 manual_bonus=1 delay=0 dropped=ai people=None:1:0:Host,AirSupport:0:3:- pings=-,85 maphash=<hex>
//  map=Matrix\Map\x.cmap" (bonus:ready:chosen side:name, "-" for no name; the manual keys 0 or 1, missing: 1;
// delay, dropped (ai or idle), auto (auto_delay) and pings may be missing: auto, ai, measuring, unknown);
// you: the receiver's index.
std::string to_line(const State &state, int you);
struct Received {
    State state;
    int you = 0;
};
std::optional<Received> from_line(std::string_view line);

std::string hello_line(BattleChoice::Bonus bonus, const std::string &data, const std::string &build,
                       const std::wstring &name = {}, const std::string &rejoin = {});
struct Hello {
    int version = 0;
    BattleChoice::Bonus bonus = BattleChoice::Bonus::None;
    std::string data, build;
    std::wstring name;  // empty or not PlayerName::valid: none
    std::string rejoin;  // the key of a person back to the battle; empty: joins the lobby
};
// nullopt when the line is not a hello.
std::optional<Hello> parse_hello(std::string_view line);

// Someone in the lobby already plays under this name (PlayerName::same).
bool name_taken(const State &state, const std::wstring &name);

// Every client is ready (the host's own flag does not count).
bool clients_ready(const State &state);

}  // namespace Lobby
