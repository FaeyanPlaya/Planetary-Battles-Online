// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Lobby.hpp"
#include "NetBattle.hpp"
#include "NetLink.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The network side of the lobby (Lobby has the lines): the host's listener and clients, a client's
// connection to the host. Nothing here blocks; poll every frame.
namespace NetLobby {

class Host {
public:
    // Opens the lobby on address:port; false when the port cannot be used.
    // data and build: this PC's fingerprints; people with others are refused. name: the host's PlayerName;
    // a person with a name already in the lobby is refused.
    bool open(const std::string &address, int port, BattleChoice::Bonus bonus, std::string data, std::string build,
              std::wstring name = {});
    // The lobby without a network: the host alone, nobody can join (poll does nothing).
    void open_local(BattleChoice::Bonus bonus, std::wstring name);
    // Takes in new people, reads what the clients say, notices who left. True when the state changed.
    bool poll();
    const Lobby::State &state() const { return m_State; }
    int people() const { return int(m_State.people.size()); }
    // The host's choices; each sends the new state. A new map makes every client not ready.
    // sides: the map's playable sides (MapSides), 0 while not read; choices it lacks are dropped.
    void set_map(const std::wstring &map, unsigned sides, const std::string &map_hash = {}, const std::string &map_pack = {});
    void set_map_sides(unsigned sides);
    // A person's side (0: any). Refused when the map lacks it; several people may choose one side
    // (MapSides::resolve draws among them at the start).
    bool choose_side(int person, int side);
    void set_ai_resources(int level);
    // Whether people may drive a robot directly, and with the driven robot's bonus.
    void set_manual(bool control, bool bonus);
    void set_bonus(BattleChoice::Bonus bonus);
    // The battle's delay in ms (whole turns); 0: auto, from the pings measured (Lobby::delay_turns).
    void set_delay(int delay);
    // Who plays a dropped person's side: the AI, or nobody (no orders) until they are back.
    void set_dropped_ai(bool ai);
    // The battle's mods; other mods make every client not ready.
    void set_mods(const std::vector<Lobby::ModRef> &mods);
    // Sends a client away (person >= 1); the same name is refused until the lobby closes.
    void kick(int person);
    // Tells the clients that the host is loading and gives up the connections: person i + 1's at index i.
    std::vector<std::unique_ptr<NetLink::Connection>> start();
    // Says goodbye to everyone.
    void close();
    // "side joined" / "left" notes for the log.
    std::vector<std::string> take_events();

private:
    struct Pending {
        std::unique_ptr<NetLink::Connection> link;
        std::chrono::steady_clock::time_point since;
    };
    // The round trip to a client: a ping every second, its pong timed; shown, the shortest of the last five
    // (the frames on both sides add to each a share of their length that the network does not).
    struct Ping {
        int number = 0;
        bool waiting = false;
        std::chrono::steady_clock::time_point sent, next;
        std::vector<int> recent;  // the last round trips, ms
        int skip = 5;             // first round trips since this person joined (or got the map file), left out of `sample`
        std::vector<int> sample;  // those since the people last changed: auto's delay (Lobby::typical_round_trip)
    };
    // The people changed: auto's delay is measured again (State::auto_delay 0 until it is).
    void measure_again();
    void settle_auto_delay();
    bool measure(std::size_t client, std::chrono::steady_clock::time_point now);
    bool pong(std::size_t client, std::string_view number, std::chrono::steady_clock::time_point now);
    void publish();
    void drop(std::size_t client);
    // mapget: starts sending the lobby's map file, or answers mapno.
    void give_map(std::size_t client, std::string_view hash);
    // The next lines of a file being sent, as many as the connection takes without a long queue.
    bool send_map(std::size_t client);
    // The lobby's map changed: sendings of the old file stop (mapno).
    void stop_sending(const std::string &old_hash);

    void drop_invalid_choices();

    std::unique_ptr<NetLink::Listener> m_Listener;
    Lobby::State m_State;
    unsigned m_MapSides = 0;
    std::string m_Data, m_Build;
    std::vector<std::unique_ptr<NetLink::Connection>> m_Clients;  // person i + 1
    std::vector<Ping> m_Pings;                                    // the same order
    std::vector<std::optional<std::size_t>> m_Sending;            // the same order: bytes of the map file sent
    std::shared_ptr<const std::string> m_MapFile;                 // the lobby's map file, read for the first asking
    bool m_MapUnreadable = false;                                 // reading it failed (or it is too large)
    std::vector<Pending> m_Pending;                               // connected, no hello yet
    std::vector<std::wstring> m_Kicked;                           // names sent away
    std::vector<std::string> m_Events;
};

class Client {
public:
    enum class Phase { Connecting, InLobby, Loading, Started, Failed };
    // ComeBack: the battle has started and this person (by name) may come back to it.
    enum class Failure { None, Unreachable, Full, Version, Build, Data, Name, Started, ComeBack, HostLeft, Lost, Kicked };

    void connect(const std::string &address, int port, BattleChoice::Bonus bonus, int timeout_ms, std::string data,
                 std::string build, std::wstring name = {});
    // True when something changed.
    bool poll();
    Phase phase() const { return m_Phase; }
    Failure failure() const { return m_Failure; }
    const Lobby::State &state() const { return m_State; }
    int you() const { return m_You; }
    bool ready() const;
    void set_bonus(BattleChoice::Bonus bonus);
    void set_ready(bool ready);
    void set_side(int side);
    // Asks the host for its map file with this fingerprint; once per fingerprint (again after another).
    void request_map(const std::string &hash);
    // The share of that file received, 0..100, while it comes; -1 otherwise.
    int map_progress() const;
    // The host could not send that file, or what came was not it.
    bool map_refused() const { return m_MapRefused; }
    // The whole file once it has come (given once).
    std::optional<std::string> take_map();
    // Once Started: the battle the host chose for this side, and the connection to the host.
    const std::optional<NetBattle::Start> &start() const { return m_Start; }
    std::unique_ptr<NetLink::Connection> take_link() { return std::move(m_Link); }
    void close();

private:
    void fail(Failure failure);

    std::unique_ptr<NetLink::Connector> m_Connector;
    std::unique_ptr<NetLink::Connection> m_Link;
    std::chrono::steady_clock::time_point m_Until, m_Retry;
    std::string m_Address;
    int m_Port = 0;
    std::string m_Data, m_Build;
    std::wstring m_Name;
    BattleChoice::Bonus m_Bonus = BattleChoice::Bonus::None;
    Phase m_Phase = Phase::Failed;
    Failure m_Failure = Failure::None;
    Lobby::State m_State;
    int m_You = 0;
    std::optional<BattleRules::Rules> m_Rules;
    std::optional<NetBattle::Start> m_Start;
    void forget_map();  // of request_map: nothing asked for

    std::string m_MapWanted;              // request_map's fingerprint
    std::string m_MapBytes;               // what has come of it
    std::size_t m_MapSize = 0;
    bool m_MapReceiving = false;
    bool m_MapRefused = false;
    std::optional<std::string> m_MapDone;
};

}  // namespace NetLobby
