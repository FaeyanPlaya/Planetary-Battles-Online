// MatrixGame - licensed under GPLv2 or any later version.
#include "Lockstep.hpp"

#include <algorithm>
#include <climits>
#include <format>
#include <sstream>
#include <utility>

namespace Lockstep {

Config usable(Config config) {
    config.delay = std::max(config.delay, 2);
    return config;
}

Session::Session(const Config &config, bool host, int side)
    : m_Config(usable(config)), m_Host(host), m_Side(side), m_Closed(m_Config.delay - 1), m_Known(m_Config.delay - 1) {}

Session Session::host(const Config &config, int side, std::vector<int> client_sides) {
    Session s(config, true, side);
    for (int client : client_sides) {
        Peer peer;
        peer.side = client;
        peer.closed = s.m_Config.delay - 1;  // turns before the delay carry no commands
        s.m_Peers.push_back(std::move(peer));
    }
    return s;
}

Session Session::client(const Config &config, int side, int host_side) {
    Session s(config, false, side);
    Peer host;
    host.side = host_side;
    s.m_Peers.push_back(std::move(host));
    return s;
}

Session Session::rejoining(const Config &config, int side, int host_side) {
    Session s = client(config, side, host_side);
    s.m_Catching = true;
    s.m_Started = true;  // the battle is under way: no go
    return s;
}

Session Session::rebuilding(const Config &config, int side, std::vector<int> client_sides) {
    Session s = host(config, side, std::move(client_sides));
    for (Peer &p : s.m_Peers) p.live = false;  // each is in again with member()
    s.m_Catching = true;
    s.m_Hold = true;
    s.m_Started = true;
    s.m_OwnReady = true;
    return s;
}

bool Session::can_rejoin(int peer) const {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size())) return false;
    const Peer &p = m_Peers[peer];
    return !p.gave_up && !p.kicked && p.how_gone >= 1;
}

void Session::rejoin(int peer) {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size())) return;
    Peer &p = m_Peers[peer];
    p.live = true;
    p.ready = true;
    p.catching = true;
    p.rejoin_turn = -1;
    p.restarted = true;
    p.tail = false;
    p.open.clear();
    p.turns.clear();
    send_history(peer, -1);
    m_Events.push_back(std::format("side {} is back, replaying the battle to turn {}", p.side, m_Known));
    tell_hold();
}

std::size_t Session::history_after(int known) const {
    const auto it = m_TurnEnd.upper_bound(known);
    return it == m_TurnEnd.begin() ? 0 : std::prev(it)->second;
}

void Session::send_history(int peer, int known) {
    for (std::size_t i = history_after(known); i < m_History.size(); ++i) send(peer, m_History[i]);
    send(peer, std::format("history {}", m_Known));
}

void Session::play_alone(const std::vector<int> &others) {
    if (m_Host || m_Solo) return;
    // Nobody else shares this battle any more: the other people's sides pass to the AI after the last whole turn,
    // and this client's own commands that never came back run here.
    m_Solo = true;
    m_Peers[0].live = false;
    m_Catching = false;
    const int next = m_Known + 1;
    for (int side : others)
        if (side != m_Side && !m_Gone.contains(side))
            m_Scheduled.push_back(surrender(side, next, side == m_Peers[0].side ? m_Peers[0].how_gone : 2));
    for (auto &[turn, commands] : m_Pending)
        for (Commands::Command &c : commands) m_Scheduled.push_back(std::move(c));
    m_Pending.clear();
    m_Incoming.clear();
    m_IncomingLines.clear();
    m_Known = INT_MAX / 2;
    m_Started = true;
    m_Events.push_back(std::format("plays on alone after turn {}", next - 1));
}

void Session::take_over(const std::vector<int> &sides) {
    if (!host_lost()) return;
    const int old_host = m_Peers[0].side;
    m_Host = true;
    m_Hold = true;
    m_Following = false;
    m_Catching = false;
    m_Peers.clear();
    for (int side : sides) {
        Peer p;
        p.side = side;
        p.live = false;
        p.closed = m_Known;
        if (const auto gone = m_Gone.find(side); gone != m_Gone.end()) {
            p.surrendered = true;
            p.how_gone = gone->second;
            p.gave_up = gone->second == 0;
            p.kicked = gone->second == 4;
        }
        m_Peers.push_back(std::move(p));
    }
    m_Incoming.clear();
    m_IncomingLines.clear();
    m_PeerHashes.clear();
    m_Waits.reset();
    m_Events.push_back(std::format("relay in the host's place (side {} gone) at turn {}", old_host, m_Known));
}

void Session::member(int peer, int known) {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size())) return;
    Peer &p = m_Peers[peer];
    p.live = true;
    p.ready = true;
    p.catching = true;  // not waited for until it has every whole turn
    p.restarted = false;
    p.rejoin_turn = -1;
    p.open.clear();
    p.turns.clear();
    p.tail_lines.clear();
    p.tail = known > m_Known;
    if (p.tail) {
        send(peer, std::format("tail {}", m_Known + 1));
        m_Events.push_back(std::format("side {} is in, ahead at turn {}: its turns from {} come here", p.side, known,
                                       m_Known + 1));
    }
    else {
        send_history(peer, known);
        m_Events.push_back(std::format("side {} is in at turn {}; it gets the turns to {}", p.side, known, m_Known));
    }
    tell_hold();
}

void Session::follow(int relay_side) {
    if (m_Host || m_Solo) return;
    Peer relay;
    relay.side = relay_side;
    m_Peers[0] = std::move(relay);
    m_Following = true;
    m_Catching = true;  // until joined: the relay's turns this peer lacks first
    m_CaughtSent = false;
    m_HistoryTurn = -1;
    m_Incoming.clear();
    m_IncomingLines.clear();
    m_Waits.reset();
    m_RelayHolds = true;  // until it says otherwise
    m_Events.push_back(std::format("follows the relay of side {} from turn {}", relay_side, m_Known));
}

void Session::hold(bool on) {
    if (!m_Host || on == m_Hold) return;
    m_Hold = on;
    m_Events.push_back(on ? "the battle holds" : std::format("the battle goes on from turn {}", m_Known + 1));
    tell_hold();
    if (!on) aggregate();
}

void Session::rebuilt() {
    if (!m_Host || !m_Catching) return;
    m_Catching = false;
    m_Closed = std::max(m_Closed, m_Known);
    // The host had left through the menu: its side is its own again (one that gave up stays the AI's).
    if (const auto gone = m_Gone.find(m_Side); gone != m_Gone.end() && gone->second != 0 && gone->second != 4)
        m_OwnRejoin = m_Known + 1;
    m_Events.push_back(std::format("rebuilt the battle to turn {}", m_Known));
    stepped(m_Now);  // its own turns up to the delay ahead, as every step closes them
}

void Session::tell_hold() {
    if (m_Host) send(-1, std::format("holds {}", m_Hold ? 1 : 0));
}

int Session::peer_of_side(int side) const {
    for (int i = 0; i < int(m_Peers.size()); ++i)
        if (m_Peers[i].side == side) return i;
    return -1;
}

int Session::side_gone(int side) const {
    const auto gone = m_Gone.find(side);
    return gone == m_Gone.end() ? -1 : gone->second;
}

bool Session::tail_awaited() const {
    return std::any_of(m_Peers.begin(), m_Peers.end(), [](const Peer &p) { return p.live && p.tail; });
}

bool Session::peer_catching_up(int peer) const { return peer_live(peer) && m_Peers[peer].catching; }

void Session::caught() {
    if (!m_Catching || m_CaughtSent || m_HistoryTurn < 0 || m_Known < m_HistoryTurn) return;
    m_CaughtSent = true;
    send(0, "caught");
}

void Session::send(int peer, std::string line) {
    if (peer >= 0 && !peer_live(peer)) return;
    m_Out.push_back({peer, std::move(line)});
}

bool Session::alone() const {
    return std::none_of(m_Peers.begin(), m_Peers.end(), [](const Peer &p) { return p.live; });
}

bool Session::peer_live(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) && m_Peers[peer].live; }
int Session::peer_side(int peer) const { return peer >= 0 && peer < int(m_Peers.size()) ? m_Peers[peer].side : 0; }

bool Session::can_step(int now) const { return m_Started && turn_of(now + m_Config.step_ms) <= m_Known; }

Commands::Command Session::surrender(int side, int turn, int how) const {
    Commands::Command c;
    c.time = turn * m_Config.turn_ms;
    c.side = side;
    c.type = Commands::Type::Surrender;
    c.params = {how};  // the side plays on under the AI
    return c;
}

std::vector<int> Session::waiting_for(int now) const {
    std::vector<int> peers;
    if (!m_Host) {
        if (m_Peers[0].live && m_Dropped == 0 && (!m_Started || !can_step(now))) peers.push_back(0);
        return peers;
    }
    if (m_Started && can_step(now)) return peers;
    for (int i = 0; i < int(m_Peers.size()); ++i) {
        const Peer &p = m_Peers[i];
        if (p.live && !p.catching && (m_Started ? p.closed <= m_Known : !p.ready)) peers.push_back(i);
    }
    return peers;
}

void Session::announce_waits(const std::vector<std::pair<int, int>> &waits) {
    if (!m_Host) return;
    std::string line = "W";
    for (std::size_t i = 0; i < waits.size(); ++i)
        line += std::format("{}{}:{}", i == 0 ? " " : ",", waits[i].first, waits[i].second);
    send(-1, std::move(line));
}

std::optional<std::vector<std::pair<int, int>>> Session::take_waits() { return std::exchange(m_Waits, std::nullopt); }

void Session::drop(int peer, bool by_host) {
    if (!m_Host || !peer_live(peer)) return;
    send(peer, std::format("dropped {}", by_host ? 2 : 1));
    m_Peers[peer].how_gone = by_host ? 3 : 2;
    m_Events.push_back(std::format("side {} removed ({})", m_Peers[peer].side,
                                   by_host ? "by the host" : "not answering"));
    lost(peer);
}

void Session::kick(int peer) {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size()) || m_Peers[peer].kicked) return;
    Peer &p = m_Peers[peer];
    p.kicked = true;
    p.rejoin_turn = -1;
    m_Events.push_back(std::format("side {} kicked out by the host", p.side));
    if (p.live) {
        send(peer, "dropped 3");
        p.how_gone = 4;
        lost(peer);  // its Surrender (4) comes with the next turn
        return;
    }
    // Gone already, their side the AI's or standing: the kick passes it to the AI for good and says so.
    p.how_gone = 4;
    p.catching = false;
    p.kick_told = false;
    aggregate();
}

void Session::set_delay(int turns) {
    turns = std::max(turns, 2);
    if (turns == m_Config.delay) return;
    m_Events.push_back(std::format("delay {} -> {} turns", m_Config.delay, turns));
    m_Config.delay = turns;
    if (m_Host) send(-1, std::format("delay {}", turns));
}

void Session::ping(int id, int lead) {
    if (!m_Host) return;
    for (int i = 0; i < int(m_Peers.size()); ++i)
        if (m_Peers[i].live && !m_Peers[i].catching) send(i, std::format("ping {} {}", id, lead));
}

std::vector<std::pair<int, int>> Session::take_pongs() { return std::exchange(m_Pongs, {}); }
std::vector<std::pair<int, int>> Session::take_desyncs() { return std::exchange(m_Desyncs, {}); }
std::vector<std::pair<int, int>> Session::take_unfixed() { return std::exchange(m_Unfixed, {}); }

void Session::resync(int peer, int time) {
    if (!m_Host || !peer_live(peer)) return;
    Peer &p = m_Peers[peer];
    if (const auto own = m_OwnHashes.find(time); own != m_OwnHashes.end()) m_Checkpoints[time] = own->second;
    p.resync_at = time;
    ++p.resyncs;
    p.desync_at = -1;
    send(peer, std::format("resync {}", time));
    m_Events.push_back(std::format("side {} went apart at t={}: it replays the battle", p.side, time));
    p.how_gone = 5;  // as a lost connection (they come back), told apart in the notices
    lost(peer);
}

void Session::unfixed(int side, int time) {
    if (!m_Host) return;
    send(-1, std::format("unfixed {} {}", side, time));
    m_Unfixed.emplace_back(side, time);
}

void Session::issue(Commands::Command command, int now) {
    int turn = turn_of(now) + m_Config.delay;
    command.side = m_Side;
    command.time = turn * m_Config.turn_ms;
    if (m_Catching && !m_Following) return;  // the side is the AI's until this person has joined
    if (m_Solo) {
        m_Scheduled.push_back(std::move(command));  // the host has gone: this client plays on alone
        return;
    }
    // Never into a turn this peer has closed (it came down from a longer delay).
    turn = std::max(turn, m_Closed + 1);
    command.time = turn * m_Config.turn_ms;
    m_Pending[turn].push_back(std::move(command));
}

void Session::close_own_turn(int turn) {
    if (m_Host) return;  // the host's own turn waits in m_Pending for aggregate()
    if (const auto it = m_Pending.find(turn); it != m_Pending.end())
        for (const Commands::Command &c : it->second) send(0, Commands::to_line(c));
    send(0, std::format("T {}", turn));
}

void Session::stepped(int now) {
    // Commands issued from now on go to turn_of(now) + delay, so every earlier turn is complete.
    const int last = turn_of(now) + m_Config.delay - 1;
    m_Now = now;
    if (m_Catching) return;  // the host's history: these turns are long decided
    while (m_Closed < last) close_own_turn(++m_Closed);
    if (m_Host) aggregate();
}

void Session::aggregate() {
    while (!m_Hold) {
        const int turn = m_Known + 1;
        if (m_Closed < turn) return;
        for (const Peer &p : m_Peers)
            if (p.live && !p.catching && p.closed < turn) return;
        std::vector<Commands::Command> all;
        // A person back from a lost connection commands the side again from this turn on: first, before the
        // side's own commands of the turn (stable side order below).
        if (m_OwnRejoin == turn) {
            m_OwnRejoin = -1;
            Commands::Command c;
            c.side = m_Side;
            c.type = Commands::Type::Rejoin;
            all.push_back(std::move(c));
        }
        for (Peer &p : m_Peers) {
            if (p.rejoin_turn != turn) continue;
            p.rejoin_turn = -1;
            if (!p.live) continue;  // gone again before it
            Commands::Command c;
            c.side = p.side;
            c.type = Commands::Type::Rejoin;
            all.push_back(std::move(c));
            p.surrendered = false;
            p.how_gone = 2;
        }
        if (auto it = m_Pending.find(turn); it != m_Pending.end()) {
            for (Commands::Command &c : it->second) all.push_back(std::move(c));
            m_Pending.erase(it);
        }
        for (Peer &p : m_Peers) {
            if (p.closed >= turn) {
                if (auto it = p.turns.find(turn); it != p.turns.end()) {
                    for (Commands::Command &c : it->second) {
                        // A person leaving through the menu says so first: no second Surrender later.
                        if (c.type == Commands::Type::Surrender && !c.params.empty() && c.params[0] >= 1)
                            p.surrendered = true;
                        if (c.type == Commands::Type::Surrender && !c.params.empty() && c.params[0] == 0)
                            p.gave_up = true;
                        all.push_back(std::move(c));
                    }
                    p.turns.erase(it);
                }
            }
            else if (!p.live && (!p.surrendered || !p.kick_told)) {
                all.push_back(surrender(p.side, turn, p.how_gone));
                p.surrendered = true;
                p.kick_told = true;
            }
        }
        // Side order, then the order each person gave them: the same on every peer.
        std::stable_sort(all.begin(), all.end(),
                         [](const Commands::Command &a, const Commands::Command &b) { return a.side < b.side; });
        for (Commands::Command &c : all) {
            c.time = turn * m_Config.turn_ms;
            m_History.push_back(Commands::to_line(c));
            send(-1, m_History.back());
        }
        m_History.push_back(std::format("A {}", turn));
        m_TurnEnd[turn] = m_History.size();
        send(-1, m_History.back());
        schedule_whole_turn(turn, std::move(all));
    }
}

void Session::schedule_whole_turn(int turn, std::vector<Commands::Command> commands) {
    for (Commands::Command &c : commands) {
        // Who is gone in the shared turns (a relay in the host's place knows so); having given up or been kicked out
        // stays so.
        if (c.type == Commands::Type::Surrender) {
            const int how = c.params.empty() ? 0 : c.params[0];
            const auto [gone, fresh] = m_Gone.try_emplace(c.side, how);
            if (!fresh && gone->second != 0 && gone->second != 4) gone->second = how;
        }
        else if (c.type == Commands::Type::Rejoin) {
            m_Gone.erase(c.side);
        }
        m_Scheduled.push_back(std::move(c));
    }
    m_Known = turn;
    m_Pending.erase(m_Pending.begin(), m_Pending.upper_bound(turn));
}

void Session::try_start() {
    if (!m_Host || m_Started || !m_OwnReady) return;
    for (const Peer &p : m_Peers)
        if (p.live && !p.ready) return;
    m_Started = true;
    send(-1, "go");
}

void Session::ready() {
    if (m_Host) {
        m_OwnReady = true;
        try_start();
    }
    else {
        send(0, "ready");
    }
}

void Session::receive(int peer, const std::string &line) {
    if (!peer_live(peer)) return;
    Peer &p = m_Peers[peer];
    std::istringstream in(line);
    std::string tag;
    in >> tag;
    if (tag == "C") {
        Commands::Command c;
        if (!Commands::from_line(line, c)) {
            m_Events.push_back(std::format("bad command from side {}: {}", p.side, line.substr(0, 200)));
        }
        else if (!m_Host) {
            m_Incoming.push_back(std::move(c));
            m_IncomingLines.push_back(line);
        }
        else if (p.tail) {
            p.tail_lines.push_back(line);  // a whole turn of the old host's, any side's
        }
        else if (c.side != p.side) {
            m_Events.push_back(std::format("side {} sent a command for side {}; ignored", p.side, c.side));
        }
        else {
            p.open.push_back(std::move(c));
        }
    }
    else if (tag == "T" && m_Host) {
        int turn = -1;
        in >> turn;
        if (turn <= m_Known && turn > p.closed) {
            // A turn decided meanwhile (a relay's tail from the old host, whose turns had these commands).
            p.closed = turn;
            p.open.clear();
            return;
        }
        if (turn != p.closed + 1) {
            m_Events.push_back(std::format("side {} sent turn {} after {}", p.side, turn, p.closed));
            return;
        }
        p.turns[turn] = std::move(p.open);
        p.open.clear();
        p.closed = turn;
        aggregate();
    }
    else if (tag == "A" && m_Host && p.tail) {
        // A whole turn this relay lacks, from a member that held more of the old host's: here, and to the others.
        int turn = -1;
        in >> turn;
        std::vector<std::string> lines = std::move(p.tail_lines);
        p.tail_lines.clear();
        if (turn != m_Known + 1) return;  // known here already (another member's tail)
        std::vector<Commands::Command> commands;
        lines.push_back(std::format("A {}", turn));
        for (std::string &l : lines) {
            Commands::Command c;
            if (l[0] == 'C' && Commands::from_line(l, c)) commands.push_back(std::move(c));
            for (int i = 0; i < int(m_Peers.size()); ++i)
                if (i != peer) send(i, l);
            m_History.push_back(std::move(l));
        }
        m_TurnEnd[turn] = m_History.size();
        schedule_whole_turn(turn, std::move(commands));
    }
    else if (tag == "A" && !m_Host) {
        int turn = -1;
        in >> turn;
        std::vector<Commands::Command> commands = std::move(m_Incoming);
        std::vector<std::string> lines = std::move(m_IncomingLines);
        m_Incoming.clear();
        m_IncomingLines.clear();
        if (turn <= m_Known) return;  // a relay's turn this peer had from the old host already
        if (turn != m_Known + 1) {
            m_Events.push_back(std::format("host sent turn {} after {}", turn, m_Known));
            return;
        }
        for (std::string &l : lines) m_History.push_back(std::move(l));
        m_History.push_back(std::format("A {}", turn));
        m_TurnEnd[turn] = m_History.size();
        schedule_whole_turn(turn, std::move(commands));
    }
    else if (tag == "tail" && !m_Host) {
        // The relay lacks whole turns this peer holds: they go there.
        int from = 0;
        in >> from;
        for (std::size_t i = history_after(from - 1); i < m_History.size(); ++i) send(0, m_History[i]);
        send(0, std::format("tail-end {}", m_Known));
    }
    else if (tag == "tail-end" && m_Host && p.tail) {
        int known = -1;
        in >> known;
        p.tail = false;
        p.tail_lines.clear();
        member(peer, std::min(known, m_Known));
    }
    else if (tag == "holds" && !m_Host) {
        int on = 0;
        in >> on;
        m_RelayHolds = on != 0;
    }
    else if (tag == "H") {
        int time = -1;
        std::uint64_t hash = 0;
        in >> time >> std::hex >> hash;
        // A person replaying the history sends hashes long forgotten here (but for a resync's time).
        if (!m_OwnHashes.empty() && time < m_OwnHashes.begin()->first && !m_Checkpoints.contains(time)) return;
        // (A hash a second for each person, gone once compared: so many are only a peer filling this PC's memory.)
        if (m_PeerHashes.size() >= 4096 && !m_PeerHashes.contains({time, peer})) return;
        m_PeerHashes[{time, peer}] = hash;
        compare(time);
    }
    else if (tag == "chat") {
        constexpr std::size_t longest = 600;  // bytes; a message is 100 characters at most
        if (m_Host) {
            std::string text = line.size() > 5 ? line.substr(5, longest) : std::string();
            if (text.empty()) return;
            for (int i = 0; i < int(m_Peers.size()); ++i)
                if (i != peer) send(i, std::format("chat {} {}", p.side, text));
            m_Chat.emplace_back(p.side, std::move(text));
        }
        else {
            int side = 0;
            in >> side;
            std::string text;
            std::getline(in >> std::ws, text);
            if (side >= 1 && !text.empty()) m_Chat.emplace_back(side, text.substr(0, longest));
        }
    }
    else if (tag == "resync" && !m_Host) {
        // The host's battle is right: this one starts again and replays it.
        in >> m_Resync;
        p.live = false;
        m_Events.push_back(std::format("the host asks for a resync: apart at t={}", m_Resync));
    }
    else if (tag == "unfixed" && !m_Host) {
        int side = 0, time = -1;
        in >> side >> time;
        m_Unfixed.emplace_back(side, time);
    }
    else if (tag == "history" && !m_Host) {
        in >> m_HistoryTurn;
    }
    else if (tag == "caught" && m_Host && p.catching) {
        // From the next turn not yet joined on, the battle waits for this person, whose side is theirs again.
        const int turn = m_Known + 1;
        p.catching = false;
        p.closed = turn - 1;
        p.rejoin_turn = p.restarted || (p.surrendered && !p.gave_up && !p.kicked) ? turn : -1;
        send(peer, std::format("joined {}", turn));
        send(peer, std::format("delay {}", m_Config.delay));  // the delay now, not the start's
        m_Events.push_back(std::format("side {} caught up; it plays from turn {}", p.side, turn));
        aggregate();
    }
    else if (tag == "joined" && !m_Host && m_Catching) {
        int turn = -1;
        in >> turn;
        m_Catching = false;
        // A member resends its turns from there (those it closed for the old host never became whole).
        m_Closed = m_Following ? turn - 1 : std::max(m_Closed, turn - 1);
        m_Events.push_back(std::format("joined the battle from turn {}", turn));
        stepped(m_Now);  // this client waits for the host's next turn, which waits for these
    }
    else if (tag == "ready" && m_Host) {
        p.ready = true;
        try_start();
    }
    else if (tag == "go" && !m_Host) {
        m_Started = true;
    }
    else if (tag == "W" && !m_Host) {
        std::vector<std::pair<int, int>> waits;
        std::string list;
        in >> list;
        std::istringstream items(list);
        std::string item;
        while (std::getline(items, item, ',')) {
            const auto colon = item.find(':');
            if (colon == std::string::npos) continue;
            waits.emplace_back(std::atoi(item.substr(0, colon).c_str()), std::atoi(item.substr(colon + 1).c_str()));
        }
        m_Waits = std::move(waits);
    }
    else if (tag == "dropped" && !m_Host) {
        // This person is out of the battle: nothing more is shared, the host side is not taken over.
        int how = 0;
        in >> how;
        m_Dropped = how == 2 || how == 3 ? how : 1;
        p.live = false;
        m_Events.push_back(how == 3   ? "the host kicked this side out of the battle"
                           : how == 2 ? "the host removed this side from the battle"
                                      : "the host removed this side from the battle: not answering");
    }
    else if (tag == "ping" && !m_Host) {
        int id = 0, lead = -1;
        in >> id;
        send(0, std::format("pong {}", id));
        if (in >> lead && lead >= 0 && m_Started && !m_Catching) m_HostLeads.push_back(lead);
    }
    else if (tag == "pong") {
        // (A lobby ping answered as the battle began has an id the host does not know.)
        int id = -1;
        in >> id;
        if (m_Host && id >= 0) m_Pongs.emplace_back(peer, id);
    }
    else if (tag == "delay" && !m_Host) {
        int turns = 0;
        in >> turns;
        if (turns >= 2 && turns != m_Config.delay) {
            m_Events.push_back(std::format("delay {} -> {} turns", m_Config.delay, turns));
            m_Config.delay = turns;
        }
    }
    else if (tag == "bye") {
        p.how_gone = 1;
        lost(peer);
    }
    else {
        m_Events.push_back(std::format("unknown line from side {}: {}", p.side, line.substr(0, 200)));
    }
}

void Session::lost(int peer) {
    if (!peer_live(peer)) return;
    Peer &p = m_Peers[peer];
    p.live = false;
    if (m_Host) {
        m_Events.push_back(std::format("side {} left after turn {}; the AI takes it over", p.side, p.closed));
        p.tail = false;
        try_start();
        aggregate();
        tell_hold();
        return;
    }
    // Without the host the battle holds at the last whole turn (host_lost): a relay takes its place, or this person
    // plays on alone.
    m_Events.push_back(std::format("host (side {}) left after turn {}; the battle holds", p.side, m_Known));
    m_Incoming.clear();
    m_IncomingLines.clear();
}

void Session::hashed(int time, std::uint64_t hash) {
    m_OwnHashes[time] = hash;
    send(m_Host ? -1 : 0, std::format("H {} {:016x}", time, hash));
    compare(time);
    // A peer's hash arrives within a few turns; older ones are no longer needed.
    m_OwnHashes.erase(m_OwnHashes.begin(), m_OwnHashes.lower_bound(time - 60000));
}

void Session::compare(int time) {
    std::uint64_t own = 0;
    if (const auto o = m_OwnHashes.find(time); o != m_OwnHashes.end()) own = o->second;
    else if (const auto c = m_Checkpoints.find(time); c != m_Checkpoints.end()) own = c->second;
    else return;
    for (auto it = m_PeerHashes.lower_bound({time, INT_MIN}); it != m_PeerHashes.end() && it->first.first == time;) {
        const int peer = it->first.second;
        if (m_Host && peer >= 0 && peer < int(m_Peers.size())) {
            // Each peer's first difference since its last resync (the host repairs it), and how a resync's replay went.
            Peer &p = m_Peers[peer];
            if (it->second != own && p.desync_at < 0) {
                p.desync_at = time;
                m_Desyncs.emplace_back(peer, time);
            }
            else if (it->second == own && time == p.resync_at) {
                m_Events.push_back(std::format("side {} replayed t={} as the host did", p.side, time));
            }
        }
        if (it->second != own && (m_FirstDesync < 0 || time - m_LastDesyncNote >= 60000)) {
            // The first mismatch, then one a minute: once apart, every later hash differs too.
            if (m_FirstDesync < 0) m_FirstDesync = time;
            m_LastDesyncNote = time;
            m_Events.push_back(std::format("desync t={} side {}: here {:016x}, there {:016x}", time,
                                           peer_side(peer), own, it->second));
        }
        it = m_PeerHashes.erase(it);
    }
}

void Session::say(const std::string &text) {
    if (text.empty() || text.find('\n') != std::string::npos) return;
    send(m_Host ? -1 : 0, m_Host ? std::format("chat {} {}", m_Side, text) : "chat " + text);
}

std::vector<std::pair<int, std::string>> Session::take_chat() { return std::exchange(m_Chat, {}); }

std::vector<Outgoing> Session::take_outgoing() { return std::exchange(m_Out, {}); }
std::vector<Commands::Command> Session::take_scheduled() { return std::exchange(m_Scheduled, {}); }
std::vector<std::string> Session::take_events() { return std::exchange(m_Events, {}); }

}  // namespace Lockstep
