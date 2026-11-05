// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Commands.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Lockstep turns of a battle shared over the network. Game time is cut into turns; a command
// issued during turn c runs at the start of turn c + delay on every peer, and a peer steps into a
// turn only once it holds that turn's commands. The host collects each turn from every peer and
// sends the whole turn back, so every peer schedules the same commands in the same order; the host
// also decides when a peer that left gives its side to the AI. No sockets here: lines in, lines out.
//
// Lines (one message each): C <command> (Commands::to_line), T <turn> (a peer's turn is complete),
// A <turn> (the host's whole turn is complete), H <time> <hash> (world hash after that game time),
// ready (loaded), go (everyone loaded: start stepping), bye (leaving), W [<side>:<ms>,...] (the host waits
// for these sides, so long; empty: no longer), dropped 1|2 (the host removed this person: 1 not
// answering for too long, 2 by the host's choice, 3 kicked out for good).
// A person whose connection was lost comes back: the host sends every turn so far and history <turn> (the
// last of them), then the live turns; the person replays the battle and says caught; the host answers
// joined <turn>: the battle waits for that person's turns from it on, and the side's Rejoin runs at its start.
// Chat beside the turns: chat <text> to the host, chat <side> <text> from it.
// The delay during the battle: the host sends ping <n> to the clients, each answers pong <n> at once; from the round
// trips the host chooses the delay and sends delay <turns>. The delay is only how far ahead a peer closes its own turns
// (what runs in a turn is the host's whole turn), so it may change at any time on any peer without a desync.
// A desync repaired: the host (always right) sends resync <time> to the person whose hash differed; they start the
// battle again and come back as after a lost connection. The host keeps its hash of that time, so the replay is
// checked there: unfixed <side> <time> to everyone says the replay went apart at the same place again.
// The host gone (a client's view): the battle holds at the last whole turn. Every peer keeps the whole turns it got,
// so a relay may take the host's place: a client (a deputy, take_over) or the host started again (rebuilding). The
// others come to it on new connections, each with its last whole turn (member): a relay behind asks for the rest
// (tail <turn>; the lines, then tail-end <turn>), one ahead sends the turns missing there as history, and the battle
// holds until the relay lets it go on (hold; it tells holds 1|0): the others in, or the host back.
namespace Lockstep {

struct Config {
    int turn_ms = 100;
    int delay = 2;     // turns between issuing a command and running it; at least 2 (see usable)
    int step_ms = 10;  // fixed simulation step; a turn is a whole number of steps
};

// A turn closes once a peer has stepped into the turn `delay` - 1 before it, and must be known before the peer steps
// into it: one turn of delay could never step (each turn would wait for itself), so it counts as two.
Config usable(Config config);

struct Outgoing {
    int peer;  // -1: every connected peer
    std::string line;
};

class Session {
public:
    // Host: its own side and the clients' sides by peer index. Client: its own side; the host is peer 0.
    static Session host(const Config &config, int side, std::vector<int> client_sides);
    static Session client(const Config &config, int side, int host_side);
    // A client coming back to a battle under way: it steps through the host's history first.
    static Session rejoining(const Config &config, int side, int host_side);
    // The host started again after it went: the battle's turns come from the clients that held it (member), and it
    // holds until rebuilt.
    static Session rebuilding(const Config &config, int side, std::vector<int> client_sides);

    // A command of this peer's side issued at game time `now`; it runs at the start of turn
    // now / turn_ms + delay (or the first turn not yet closed here, after the delay came down).
    void issue(Commands::Command command, int now);
    // After every simulation step: closes this peer's turns that can no longer get commands.
    void stepped(int now);
    void receive(int peer, const std::string &line);
    // The connection to a peer is gone (closed, failed or said bye).
    void lost(int peer);
    // Host: removes a peer that keeps the battle waiting, by the host's choice (`by_host`) or for not
    // answering too long; the peer is told so and its side passes to the AI. The connection is closed
    // by the caller once the line is sent (peer_live is false from now on).
    void drop(int peer, bool by_host);
    // Host: puts the peer's person out of the battle for good, in it or gone: told so if live, the side to the AI,
    // never back (someone coming back again and again to spite the others).
    void kick(int peer);
    bool peer_kicked(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) && m_Peers[peer].kicked; }
    // This peer has loaded the battle.
    void ready();
    // World hash after game time `time`, compared with the other peers' hashes for that time.
    void hashed(int time, std::uint64_t hash);

    bool started() const { return m_Started; }
    // True when the step from `now` to now + step_ms may run: its turn's commands are all known.
    bool can_step(int now) const;
    int known_turn() const { return m_Known; }
    int delay() const { return m_Config.delay; }
    // Host: the delay from now on (at least 2 turns), for every peer. A lower one takes hold as time reaches the turns
    // closed already; a higher one at once.
    void set_delay(int turns);
    // Host: measures the round trip to the live clients (each answers pong <id>); their answers as (peer, id), once.
    void ping(int id);
    std::vector<std::pair<int, int>> take_pongs();
    bool is_host() const { return m_Host; }
    int side() const { return m_Side; }
    bool alone() const;  // every other peer has gone
    bool peer_live(int peer) const;
    int peer_side(int peer) const;
    int peer_count() const { return int(m_Peers.size()); }
    int first_desync() const { return m_FirstDesync; }  // game time, -1: none
    // Host: each peer's first hash unlike the host's since its last resync, as (peer, game time), once.
    std::vector<std::pair<int, int>> take_desyncs();
    // Host: the peer's battle went apart at `time`: they start it again and replay it (a lost connection, may come back).
    void resync(int peer, int time);
    int peer_resync_at(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) ? m_Peers[peer].resync_at : -1; }
    int peer_resyncs(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) ? m_Peers[peer].resyncs : 0; }
    // Host: tells everyone that the side's replay went apart again at `time` (no more repairing it).
    void unfixed(int side, int time);
    // Client: the host's resync: the game time its battle went apart there; -1: none.
    int resync_asked() const { return m_Resync; }
    // Every peer: (side, time) of the host's unfixed lines (the host's own too), once.
    std::vector<std::pair<int, int>> take_unfixed();

    // Client: the host has gone (it did not remove this person): the battle holds at the last whole turn while a relay
    // is looked for; play_alone ends that: the other people's sides (`others`, the host's among them) pass to the AI
    // and this person's orders run here.
    bool host_lost() const { return !m_Host && !m_Solo && !m_Peers[0].live && m_Dropped == 0 && m_Resync < 0; }
    void play_alone(const std::vector<int> &others);
    bool solo() const { return m_Solo; }
    // Client with the host lost: this peer is the relay of the people of `sides` (the old host's among them), the
    // battle holding; none is in until member().
    void take_over(const std::vector<int> &sides);
    // Relay: the person of the peer is in on a new connection, holding the whole turns to `known`.
    void member(int peer, int known);
    // Client with the host lost: the relay of `side` is peer 0 now (the connection made, its takeover line sent).
    void follow(int relay_side);
    // Relay: the battle holds (no whole turns) or goes on.
    void hold(bool on);
    bool holding() const { return m_Hold; }
    // Host rebuilding: the turns it has are replayed; it plays from the next one.
    void rebuilt();
    bool rebuilding_now() const { return m_Host && m_Catching; }
    // The battle holds in the host's absence: at a relay its hold, at a member its relay's word (holds 0: not).
    bool group_holds() const { return m_Host ? m_Hold : m_RelayHolds && m_Peers[0].live; }
    // The peer of a side's person, -1: none.
    int peer_of_side(int side) const;
    // A side's person left in the shared turns (their Surrender, not yet back): how (the Surrender's param), -1 not.
    int side_gone(int side) const;
    // Relay: someone sends turns this peer lacks (tail): not answered until they are in.
    bool tail_awaited() const;

    // Host: the live peers the battle waits for now: before the start those not loaded yet, then those
    // whose next turn is not in yet. Client: the host when this client cannot step.
    std::vector<int> waiting_for(int now) const;
    // Host: tells the clients whom it waits for, (side, milliseconds) each; an empty list: no longer.
    void announce_waits(const std::vector<std::pair<int, int>> &waits);
    // Client: the host's latest W line, once (nullopt: none since the last call).
    std::optional<std::vector<std::pair<int, int>>> take_waits();
    // Client: the host has removed this person from the battle: 1 for not answering, 2 by its choice, 3 kicked out
    // for good; 0 not.
    int dropped() const { return m_Dropped; }

    // Host: the peer's person may come back, however they went (left, lost the connection, did not answer, the host went
    // on without them), unless they gave up or the host kicked them out.
    bool can_rejoin(int peer) const;
    // Host: the peer is back on a new connection: it gets the battle's turns so far, then the live ones; the
    // battle does not wait for it until it has caught up.
    void rejoin(int peer);
    // Client: still replaying the host's history (until joined).
    bool catching_up() const { return m_Catching; }
    // Client: the last turn of the host's history, -1 until it came; how far this client has got.
    int history_turn() const { return m_HistoryTurn; }
    // Client: tells the host once that the history is replayed (call when it cannot step any more).
    void caught();
    // Host: the peer is replaying the history.
    bool peer_catching_up(int peer) const;
    // Chat: this person's message (UTF-8, one line) to everyone; nothing in the battle. The host passes each
    // message on to the other peers. The others' messages as (side, text), once.
    void say(const std::string &text);
    std::vector<std::pair<int, std::string>> take_chat();
    // How a peer that is gone went: 1 said bye, 2 the connection was lost (or it did not answer), 3 removed, 4 kicked out,
    // 5 replaying the battle after a desync.
    int peer_how_gone(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) ? m_Peers[peer].how_gone : 0; }

    std::vector<Outgoing> take_outgoing();
    // Commands to pass to Commands::schedule, in this order.
    std::vector<Commands::Command> take_scheduled();
    // Notes for the log and the trace: peers leaving, hash mismatches.
    std::vector<std::string> take_events();

private:
    struct Peer {
        int side = 0;
        bool live = true;
        bool ready = false;
        bool surrendered = false;  // this side's leaving Surrender is in a turn (its own or the host's)
        bool gave_up = false;      // its person gave up (and watched on)
        int how_gone = 2;          // Surrender param once gone: 1 left (bye), 2 lost the connection, 3 removed by the host,
                                   // 4 kicked out for good
        bool kicked = false;       // the host kicked the person out: never back
        bool kick_told = true;     // the side's kicked-out Surrender is in a turn (false: due in the next one)
        int desync_at = -1;        // its first hash unlike the host's since the last resync
        int resync_at = -1;        // the time its last resync was for
        int resyncs = 0;
        bool restarted = false;    // came back after starting the game again: its side's Rejoin gives it the orders
        bool tail = false;         // sending the whole turns this relay lacks
        std::vector<std::string> tail_lines;  // C lines of the tail's turn being received
        bool catching = false;     // came back, replaying the history: not waited for
        int rejoin_turn = -1;      // the turn whose start runs the side's Rejoin
        int closed = 0;            // last turn this peer completed
        std::vector<Commands::Command> open;  // commands of the turn being received
        std::map<int, std::vector<Commands::Command>> turns;
    };

    Session(const Config &config, bool host, int side);
    void send(int peer, std::string line);
    void close_own_turn(int turn);
    void aggregate();
    void try_start();
    void compare(int time);
    void schedule_whole_turn(int turn, std::vector<Commands::Command> commands);
    // The whole turns after `known`, then history <last>.
    void send_history(int peer, int known);
    std::size_t history_after(int known) const;  // m_History's index of the first line after turn `known`
    void tell_hold();
    // The Surrender of a side whose person is gone: param 1 left (said bye), 2 lost the connection, 3
    // removed by the host, 4 kicked out.
    Commands::Command surrender(int side, int turn, int how) const;
    int turn_of(int now) const { return now / m_Config.turn_ms; }

    Config m_Config;
    bool m_Host;
    int m_Side;
    bool m_Started = false;
    bool m_OwnReady = false;
    int m_Closed;  // last own turn sent (host: collected)
    int m_Known;   // last turn whose commands are all scheduled here
    std::map<int, std::vector<Commands::Command>> m_Pending;  // own commands by turn, until known
    std::vector<Peer> m_Peers;
    std::vector<Commands::Command> m_Incoming;  // client: commands of the host turn being received
    std::optional<std::vector<std::pair<int, int>>> m_Waits;  // client: the host's latest W line
    int m_Dropped = 0;
    std::vector<std::string> m_History;  // every whole turn so far (C and A lines; a client's as the host sent them)
    std::map<int, std::size_t> m_TurnEnd;  // turn -> m_History's size after its A line
    std::vector<std::string> m_IncomingLines;  // client: the C lines of m_Incoming
    std::map<int, int> m_Gone;  // side -> its Surrender's param, while gone in the shared turns
    bool m_Solo = false;        // client: plays on alone, the host gone
    bool m_Following = false;   // client: a relay's member (on the old session: its orders are kept)
    bool m_Hold = false;        // relay: no whole turns until it goes on
    int m_OwnRejoin = -1;       // host rebuilt: the turn whose start runs its own side's Rejoin
    bool m_RelayHolds = false;  // member: the relay holds the battle (until its holds 0)
    bool m_Catching = false;             // client: replaying the history, not yet joined
    bool m_CaughtSent = false;
    int m_HistoryTurn = -1;
    int m_Now = 0;  // the game time of the last step
    std::vector<std::pair<int, std::string>> m_Chat;
    std::vector<std::pair<int, int>> m_Pongs;  // host: (peer, id)
    std::vector<std::pair<int, int>> m_Desyncs;  // host: (peer, time)
    std::vector<std::pair<int, int>> m_Unfixed;  // (side, time)
    std::map<int, std::uint64_t> m_Checkpoints;  // host: its hashes of the times a resync was for, kept
    int m_Resync = -1;
    std::map<int, std::uint64_t> m_OwnHashes;
    std::map<std::pair<int, int>, std::uint64_t> m_PeerHashes;  // (time, peer)
    int m_FirstDesync = -1;
    int m_LastDesyncNote = -1;
    std::vector<Outgoing> m_Out;
    std::vector<Commands::Command> m_Scheduled;
    std::vector<std::string> m_Events;
};

}  // namespace Lockstep
