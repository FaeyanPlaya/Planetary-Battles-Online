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

bool Session::can_rejoin(int peer) const {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size())) return false;
    const Peer &p = m_Peers[peer];
    return !p.gave_up && (p.how_gone == 2 || p.how_gone == 3);  // lost, or the host went on without them
}

void Session::rejoin(int peer) {
    if (!m_Host || peer < 0 || peer >= int(m_Peers.size())) return;
    Peer &p = m_Peers[peer];
    p.live = true;
    p.ready = true;
    p.catching = true;
    p.rejoin_turn = -1;
    p.open.clear();
    p.turns.clear();
    for (const std::string &line : m_History) send(peer, line);
    send(peer, std::format("history {}", m_Known));
    m_Events.push_back(std::format("side {} is back, replaying the battle to turn {}", p.side, m_Known));
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

void Session::issue(Commands::Command command, int now) {
    const int turn = turn_of(now) + m_Config.delay;
    command.side = m_Side;
    command.time = turn * m_Config.turn_ms;
    if (m_Catching) return;  // the side is the AI's until this person has joined
    if (!m_Host && !m_Peers[0].live) {
        m_Scheduled.push_back(std::move(command));  // the host has gone: this client plays on alone
        return;
    }
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
    while (true) {
        const int turn = m_Known + 1;
        if (m_Closed < turn) return;
        for (const Peer &p : m_Peers)
            if (p.live && !p.catching && p.closed < turn) return;
        std::vector<Commands::Command> all;
        // A person back from a lost connection commands the side again from this turn on: first, before the
        // side's own commands of the turn (stable side order below).
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
            else if (!p.live && !p.surrendered) {
                all.push_back(surrender(p.side, turn, p.how_gone));
                p.surrendered = true;
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
        send(-1, m_History.back());
        schedule_whole_turn(turn, std::move(all));
    }
}

void Session::schedule_whole_turn(int turn, std::vector<Commands::Command> commands) {
    for (Commands::Command &c : commands) m_Scheduled.push_back(std::move(c));
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
            m_Events.push_back(std::format("bad command from side {}: {}", p.side, line));
        }
        else if (!m_Host) {
            m_Incoming.push_back(std::move(c));
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
        if (turn != p.closed + 1) {
            m_Events.push_back(std::format("side {} sent turn {} after {}", p.side, turn, p.closed));
            return;
        }
        p.turns[turn] = std::move(p.open);
        p.open.clear();
        p.closed = turn;
        aggregate();
    }
    else if (tag == "A" && !m_Host) {
        int turn = -1;
        in >> turn;
        if (turn != m_Known + 1) {
            m_Events.push_back(std::format("host sent turn {} after {}", turn, m_Known));
            return;
        }
        std::vector<Commands::Command> commands = std::move(m_Incoming);
        m_Incoming.clear();
        schedule_whole_turn(turn, std::move(commands));
    }
    else if (tag == "H") {
        int time = -1;
        std::uint64_t hash = 0;
        in >> time >> std::hex >> hash;
        // A person replaying the history sends hashes long forgotten here.
        if (!m_OwnHashes.empty() && time < m_OwnHashes.begin()->first) return;
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
    else if (tag == "history" && !m_Host) {
        in >> m_HistoryTurn;
    }
    else if (tag == "caught" && m_Host && p.catching) {
        // From the next turn not yet joined on, the battle waits for this person, whose side is theirs again.
        const int turn = m_Known + 1;
        p.catching = false;
        p.closed = turn - 1;
        p.rejoin_turn = turn;
        send(peer, std::format("joined {}", turn));
        m_Events.push_back(std::format("side {} caught up; it plays from turn {}", p.side, turn));
        aggregate();
    }
    else if (tag == "joined" && !m_Host && m_Catching) {
        int turn = -1;
        in >> turn;
        m_Catching = false;
        m_Closed = std::max(m_Closed, turn - 1);
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
        m_Dropped = how == 2 ? 2 : 1;
        p.live = false;
        m_Events.push_back(how == 2 ? "the host removed this side from the battle"
                                    : "the host removed this side from the battle: not answering");
    }
    else if (tag == "pong") {
        // A lobby ping answered as the battle began: nothing to do.
    }
    else if (tag == "bye") {
        p.how_gone = 1;
        lost(peer);
    }
    else {
        m_Events.push_back(std::format("unknown line from side {}: {}", p.side, line));
    }
}

void Session::lost(int peer) {
    if (!peer_live(peer)) return;
    Peer &p = m_Peers[peer];
    p.live = false;
    if (m_Host) {
        m_Events.push_back(std::format("side {} left after turn {}; the AI takes it over", p.side, p.closed));
        try_start();
        aggregate();
        return;
    }
    // Without the host nobody else shares this battle: the host's side passes to the AI after the
    // last turn it sent, and this client's own commands that the host never returned run here.
    const int next = m_Known + 1;
    m_Events.push_back(std::format("host (side {}) left after turn {}; the AI takes it over", p.side, m_Known));
    m_Scheduled.push_back(surrender(p.side, next, p.how_gone));
    for (auto &[turn, commands] : m_Pending)
        for (Commands::Command &c : commands) m_Scheduled.push_back(std::move(c));
    m_Pending.clear();
    m_Incoming.clear();
    m_Known = INT_MAX / 2;
    m_Started = true;
}

void Session::hashed(int time, std::uint64_t hash) {
    m_OwnHashes[time] = hash;
    send(m_Host ? -1 : 0, std::format("H {} {:016x}", time, hash));
    compare(time);
    // A peer's hash arrives within a few turns; older ones are no longer needed.
    m_OwnHashes.erase(m_OwnHashes.begin(), m_OwnHashes.lower_bound(time - 60000));
}

void Session::compare(int time) {
    const auto own = m_OwnHashes.find(time);
    if (own == m_OwnHashes.end()) return;
    for (auto it = m_PeerHashes.lower_bound({time, INT_MIN}); it != m_PeerHashes.end() && it->first.first == time;) {
        if (it->second != own->second && (m_FirstDesync < 0 || time - m_LastDesyncNote >= 60000)) {
            // The first mismatch, then one a minute: once apart, every later hash differs too.
            if (m_FirstDesync < 0) m_FirstDesync = time;
            m_LastDesyncNote = time;
            m_Events.push_back(std::format("desync t={} side {}: here {:016x}, there {:016x}", time,
                                           peer_side(it->first.second), own->second, it->second));
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
