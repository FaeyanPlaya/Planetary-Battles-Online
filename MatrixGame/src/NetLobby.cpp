// MatrixGame - licensed under GPLv2 or any later version.
#include "NetLobby.hpp"

#include "Lockstep.hpp"
#include "MapShare.hpp"
#include "PlayerName.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace NetLobby {
namespace {

constexpr auto hello_wait = std::chrono::seconds(10);
constexpr auto retry_every = std::chrono::milliseconds(500);
constexpr auto ping_every = std::chrono::seconds(1);
constexpr auto ping_skipping = std::chrono::milliseconds(300);  // a new person's first round trips
constexpr auto ping_sampling = std::chrono::milliseconds(500);  // while auto's delay is measured
constexpr std::size_t auto_samples = 8;  // Lobby::typical_round_trip takes the middle one
constexpr auto ping_lost = std::chrono::seconds(5);  // no pong so long: ask again
constexpr std::size_t send_queue = 2 * MapShare::chunk_size;  // a map file's lines wait behind no more

void refuse(NetLink::Connection &link, const std::string &reason) {
    link.send_line("refused " + reason);
    link.flush(500);
}

}  // namespace

bool Host::open(const std::string &address, int port, BattleChoice::Bonus bonus, std::string data, std::string build,
                std::wstring name) {
    m_Data = std::move(data);
    m_Build = std::move(build);
    m_Listener = std::make_unique<NetLink::Listener>(address, port);
    if (!m_Listener->listening()) {
        m_Listener.reset();
        return false;
    }
    m_State = {};
    m_State.people.push_back({bonus, true, 0, std::move(name)});
    measure_again();
    return true;
}

void Host::open_local(BattleChoice::Bonus bonus, std::wstring name) {
    close();
    m_State.people.push_back({bonus, true, 0, std::move(name)});
}

bool Host::poll() {
    if (!m_Listener) return false;
    bool changed = false;
    const auto now = std::chrono::steady_clock::now();
    while (auto link = m_Listener->accept(0)) m_Pending.push_back({std::move(link), now});

    for (std::size_t i = 0; i < m_Pending.size();) {
        auto &link = *m_Pending[i].link;
        const bool open = link.pump();
        std::string line;
        if (link.next_line(line)) {
            const auto hello = Lobby::parse_hello(line);
            if (!hello) {
                m_Events.push_back("a connection did not introduce itself: " + line);
            }
            else if (hello->version != Lobby::protocol_version) {
                refuse(link, std::format("version {}", Lobby::protocol_version));
                m_Events.push_back(std::format("refused a client with protocol {}", hello->version));
            }
            else if (hello->build != m_Build) {
                refuse(link, "build");
                m_Events.push_back("refused a client with another build " + hello->build);
            }
            else if (hello->data != m_Data) {
                refuse(link, "data");
                m_Events.push_back("refused a client with other game files " + hello->data);
            }
            else if (people() >= Lobby::max_people) {
                refuse(link, "full");
                m_Events.push_back("refused a client: the lobby is full");
            }
            else if (!hello->name.empty() && std::any_of(m_Kicked.begin(), m_Kicked.end(), [&](const std::wstring &k) {
                         return PlayerName::same(k, hello->name);
                     })) {
                refuse(link, "kicked");
                m_Events.push_back("refused a client the host sent away");
            }
            else if (Lobby::name_taken(m_State, hello->name)) {
                refuse(link, "name");
                m_Events.push_back("refused a client: its name is taken");
            }
            else {
                m_State.people.push_back({hello->bonus, false, 0, hello->name});
                m_Clients.push_back(std::move(m_Pending[i].link));
                m_Pings.push_back({});
                m_Sending.push_back(std::nullopt);
                m_Events.push_back(std::format("person {} joined", people()));
                measure_again();
                changed = true;
            }
            m_Pending.erase(m_Pending.begin() + std::ptrdiff_t(i));
            continue;
        }
        if (!open || now - m_Pending[i].since > hello_wait) {
            m_Pending.erase(m_Pending.begin() + std::ptrdiff_t(i));
            continue;
        }
        ++i;
    }

    for (std::size_t i = m_Clients.size(); i-- > 0;) {
        auto &link = *m_Clients[i];
        bool open = link.pump();
        Lobby::Person &person = m_State.people[i + 1];
        std::string line;
        while (open && link.next_line(line)) {
            if (line == "bye") open = false;
            else if (line.starts_with("bonus ")) {
                const auto bonus = BattleRules::parse_bonus(std::string_view(line).substr(6));
                if (bonus && *bonus != person.bonus) {
                    person.bonus = *bonus;
                    changed = true;
                }
            }
            else if (line.starts_with("side ")) {
                int side = -1;
                std::from_chars(line.data() + 5, line.data() + line.size(), side);
                const bool taken = choose_side(int(i) + 1, side);
                m_Events.push_back(std::format("person {} asks for side {}: {}", i + 2, side, taken ? "given" : "refused"));
                if (taken) changed = true;
            }
            else if (line.starts_with("pong ")) {
                if (pong(i, std::string_view(line).substr(5), std::chrono::steady_clock::now())) changed = true;
            }
            else if (line.starts_with("mapget ")) {
                give_map(i, std::string_view(line).substr(7));
            }
            else if (line == "ready 0" || line == "ready 1") {
                const bool ready = line == "ready 1";
                if (ready != person.ready) {
                    person.ready = ready;
                    changed = true;
                }
            }
        }
        if (!open) {
            drop(i);
            changed = true;
            continue;
        }
        if (send_map(i)) changed = true;
        measure(i, now);
    }
    if (changed) publish();
    return changed;
}

void Host::give_map(std::size_t client, std::string_view hash) {
    auto &link = *m_Clients[client];
    if (hash.empty() || hash != m_State.map_hash || m_State.map.empty()) {
        link.send_line("mapno " + std::string(hash));
        return;
    }
    if (!m_MapFile && !m_MapUnreadable) {
        auto bytes = MapShare::read(m_State.map);
        if (bytes && MapShare::fingerprint(*bytes) == m_State.map_hash)
            m_MapFile = std::make_shared<const std::string>(std::move(*bytes));
        else
            m_MapUnreadable = true;
    }
    if (!m_MapFile) {
        link.send_line("mapno " + std::string(hash));
        m_Events.push_back(std::format("cannot send the map to person {}", client + 2));
        return;
    }
    link.send_line(std::format("mapfile {} {}", m_State.map_hash, m_MapFile->size()));
    m_Sending[client] = 0;
    m_Events.push_back(std::format("sending the map ({} bytes) to person {}", m_MapFile->size(), client + 2));
}

bool Host::send_map(std::size_t client) {
    auto &at = m_Sending[client];
    if (!at || !m_MapFile) return false;
    auto &link = *m_Clients[client];
    const std::string_view file = *m_MapFile;
    while (*at < file.size() && link.queued() < send_queue) {
        const std::size_t n = std::min(MapShare::chunk_size, file.size() - *at);
        link.send_line("mapdata " + MapShare::to_base64(file.substr(*at, n)));
        *at += n;
    }
    if (*at < file.size()) {
        link.pump();
        return false;
    }
    link.send_line("mapend " + m_State.map_hash);
    link.pump();
    at.reset();
    m_Events.push_back(std::format("sent the map to person {}", client + 2));
    // The file's lines held the pings back, and the client reads the file now: auto's delay is measured again,
    // without this person's first round trips (as after joining).
    m_Pings[client].skip = 5;
    measure_again();
    return true;
}

void Host::stop_sending(const std::string &old_hash) {
    m_MapFile.reset();
    m_MapUnreadable = false;
    for (std::size_t i = 0; i < m_Sending.size(); ++i) {
        if (!m_Sending[i]) continue;
        m_Sending[i].reset();
        m_Clients[i]->send_line("mapno " + old_hash);
    }
}

bool Host::measure(std::size_t client, std::chrono::steady_clock::time_point now) {
    Ping &p = m_Pings[client];
    if (now < p.next || (p.waiting && now - p.sent < ping_lost)) return false;
    p.number = p.number % 1000000 + 1;
    p.waiting = true;
    p.sent = now;
    p.next = now + (p.skip > 0 ? ping_skipping : p.sample.size() < auto_samples ? ping_sampling : ping_every);
    m_Clients[client]->send_line(std::format("ping {}", p.number));
    m_Clients[client]->pump();
    return true;
}

bool Host::pong(std::size_t client, std::string_view number, std::chrono::steady_clock::time_point now) {
    Ping &p = m_Pings[client];
    int n = 0;
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), n);
    if (error != std::errc{} || !p.waiting || n != p.number) return false;
    p.waiting = false;
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now - p.sent).count());
    p.recent.push_back(ms);
    if (p.recent.size() > 5) p.recent.erase(p.recent.begin());
    const int before = m_State.auto_delay;
    if (p.skip > 0)
        --p.skip;
    else if (p.sample.size() < auto_samples) {
        p.sample.push_back(ms);
        settle_auto_delay();
    }
    int &shown = m_State.people[client + 1].ping;
    const int best = *std::min_element(p.recent.begin(), p.recent.end());
    if (best == shown && before == m_State.auto_delay) return false;
    shown = best;
    return true;
}

void Host::measure_again() {
    for (Ping &p : m_Pings) {
        p.sample.clear();
        p.next = {};  // sooner than the next second
    }
    m_State.auto_delay = 0;
    settle_auto_delay();  // (alone: at once)
}

void Host::settle_auto_delay() {
    if (m_State.auto_delay > 0) return;
    int slowest = 0;
    std::string samples;  // for the log: each person's round trips
    for (const Ping &p : m_Pings) {
        if (p.sample.size() < auto_samples) return;
        slowest = std::max(slowest, Lobby::typical_round_trip(p.sample));
        samples += samples.empty() ? "" : "; ";
        for (std::size_t i = 0; i < p.sample.size(); ++i) samples += std::format("{}{}", i ? " " : "", p.sample[i]);
    }
    m_State.auto_delay = Lobby::auto_delay_for(slowest, Lockstep::Config{}.turn_ms);
    m_Events.push_back(std::format("auto delay {} ms (slowest typical round trip {} ms; round trips {})",
                                   m_State.auto_delay, slowest, samples.empty() ? "-" : samples));
}

void Host::drop(std::size_t client) {
    m_Clients.erase(m_Clients.begin() + std::ptrdiff_t(client));
    m_Pings.erase(m_Pings.begin() + std::ptrdiff_t(client));
    m_Sending.erase(m_Sending.begin() + std::ptrdiff_t(client));
    m_State.people.erase(m_State.people.begin() + std::ptrdiff_t(client) + 1);
    m_Events.push_back(std::format("person {} left", client + 2));
    measure_again();
}

void Host::kick(int person) {
    if (person < 1 || person >= people()) return;
    const std::size_t client = std::size_t(person - 1);
    m_Clients[client]->send_line("kicked");
    m_Clients[client]->flush(500);
    if (!m_State.people[std::size_t(person)].name.empty()) m_Kicked.push_back(m_State.people[std::size_t(person)].name);
    m_Events.push_back(std::format("the host sends person {} away", person + 1));
    drop(client);
    publish();
}

void Host::publish() {
    for (std::size_t i = 0; i < m_Clients.size(); ++i) {
        m_Clients[i]->send_line(Lobby::to_line(m_State, int(i) + 1));
        m_Clients[i]->pump();
    }
}

void Host::set_map(const std::wstring &map, unsigned sides, const std::string &map_hash, const std::string &map_pack) {
    if (map == m_State.map) {
        if (map_hash != m_State.map_hash || map_pack != m_State.map_pack) {
            stop_sending(m_State.map_hash);
            m_State.map_hash = map_hash;
            m_State.map_pack = map_pack;
            publish();
        }
        set_map_sides(sides);
        return;
    }
    stop_sending(m_State.map_hash);
    m_State.map = map;
    m_State.map_hash = map_hash;
    m_State.map_pack = map_pack;
    m_MapSides = sides;
    drop_invalid_choices();
    // Everyone agrees to the map anew.
    for (std::size_t i = 1; i < m_State.people.size(); ++i) m_State.people[i].ready = false;
    publish();
}

void Host::set_map_sides(unsigned sides) {
    if (sides == m_MapSides) return;
    m_MapSides = sides;
    drop_invalid_choices();
    publish();
}

void Host::drop_invalid_choices() {
    for (Lobby::Person &p : m_State.people)
        if (p.side && m_MapSides && !(m_MapSides & (1u << p.side))) p.side = 0;
}

bool Host::choose_side(int person, int side) {
    if (person < 0 || person >= people() || side < 0 || side > BattleRules::side_count) return false;
    if (side && (!(m_MapSides & (1u << side)))) return false;
    if (m_State.people[person].side == side) return false;
    m_State.people[person].side = side;
    publish();
    return true;
}

void Host::set_ai_resources(int level) {
    if (level == m_State.ai_resources) return;
    m_State.ai_resources = level;
    publish();
}

void Host::set_manual(bool control, bool bonus) {
    if (control == m_State.manual_control && bonus == m_State.manual_bonus) return;
    m_State.manual_control = control;
    m_State.manual_bonus = bonus;
    publish();
}

void Host::set_delay(int delay) {
    if (delay == m_State.delay) return;
    m_State.delay = delay;
    publish();
}

void Host::set_dropped_ai(bool ai) {
    if (ai == m_State.dropped_ai) return;
    m_State.dropped_ai = ai;
    publish();
}

void Host::set_mods(const std::vector<Lobby::ModRef> &mods) {
    if (mods == m_State.mods) return;
    m_State.mods = mods;
    // Everyone agrees to the mods anew.
    for (std::size_t i = 1; i < m_State.people.size(); ++i) m_State.people[i].ready = false;
    publish();
}

void Host::set_bonus(BattleChoice::Bonus bonus) {
    if (bonus == m_State.people[0].bonus) return;
    m_State.people[0].bonus = bonus;
    publish();
}

std::vector<std::unique_ptr<NetLink::Connection>> Host::start() {
    for (auto &link : m_Clients) {
        link->send_line("loading");
        link->flush(1000);
    }
    m_Listener.reset();
    m_Pending.clear();
    m_Pings.clear();
    m_Sending.clear();
    m_MapFile.reset();
    return std::move(m_Clients);
}

void Host::close() {
    for (auto &link : m_Clients) {
        link->send_line("bye");
        link->flush(500);
    }
    m_Clients.clear();
    m_Pings.clear();
    m_Sending.clear();
    m_MapFile.reset();
    m_MapUnreadable = false;
    m_Pending.clear();
    m_Kicked.clear();
    m_Listener.reset();
    m_State = {};
}

std::vector<std::string> Host::take_events() { return std::exchange(m_Events, {}); }

void Client::connect(const std::string &address, int port, BattleChoice::Bonus bonus, int timeout_ms, std::string data,
                     std::string build, std::wstring name) {
    close();
    m_Data = std::move(data);
    m_Build = std::move(build);
    m_Name = std::move(name);
    m_Connector = std::make_unique<NetLink::Connector>(address, port);
    m_Until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    m_Bonus = bonus;
    m_Phase = Phase::Connecting;
    m_Failure = Failure::None;
    m_State = {};
    m_You = 0;
    m_Rules.reset();
    m_Start.reset();
    forget_map();
    m_Address = address;
    m_Port = port;
    m_Retry = {};
}

bool Client::poll() {
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    if (m_Phase == Phase::Connecting && !m_Link) {
        if (m_Connector) {
            if (auto link = m_Connector->poll()) {
                m_Link = std::move(link);
                m_Connector.reset();
                m_Link->send_line(Lobby::hello_line(m_Bonus, m_Data, m_Build, m_Name));
            }
            else if (m_Connector->failed()) {
                m_Connector.reset();
                m_Retry = now + retry_every;  // the host may still be opening its lobby
            }
        }
        else if (now >= m_Retry) {
            m_Connector = std::make_unique<NetLink::Connector>(m_Address, m_Port);
        }
        if (!m_Link && now >= m_Until) {
            fail(Failure::Unreachable);
            return true;
        }
    }
    if (!m_Link || m_Phase == Phase::Started || m_Phase == Phase::Failed) return changed;

    const bool open = m_Link->pump();
    std::string line;
    // After the start line the rest is the battle's: it stays in the connection for NetBattle.
    while (m_Phase != Phase::Started && m_Phase != Phase::Failed && m_Link->next_line(line)) {
        if (auto received = Lobby::from_line(line)) {
            const bool new_map = received->state.map_hash != m_State.map_hash;
            m_State = std::move(received->state);
            m_You = received->you;
            // The host chose another map: what came of the last one asked for is not handed over (it would be checked
            // against this map's fingerprint and this map marked failed), and its late refusal does not count (the
            // host comes back to it: asked again).
            if (new_map) forget_map();
            if (m_Phase == Phase::Connecting) m_Phase = Phase::InLobby;
            changed = true;
        }
        else if (line.starts_with("refused")) {
            fail(line.find("kicked") != std::string::npos    ? Failure::Kicked
                 : line.find("name") != std::string::npos    ? Failure::Name
                 : line.find("full") != std::string::npos    ? Failure::Full
                 : line.find("version") != std::string::npos ? Failure::Version
                 : line.find("build") != std::string::npos   ? Failure::Build
                 : line.find("data") != std::string::npos    ? Failure::Data
                 : line.find("back") != std::string::npos    ? Failure::ComeBack
                                                             : Failure::Started);
            return true;
        }
        else if (line.starts_with("ping ")) {
            m_Link->send_line("pong " + line.substr(5));  // at once: the host times the answer
            m_Link->pump();
        }
        else if (line.starts_with("mapdata ")) {
            if (!m_MapReceiving) continue;
            const int before = map_progress();
            const auto bytes = MapShare::from_base64(std::string_view(line).substr(8));
            if (!bytes || m_MapBytes.size() + bytes->size() > m_MapSize) {
                m_MapReceiving = false;
                m_MapRefused = true;
                m_MapBytes.clear();
                changed = true;
                continue;
            }
            m_MapBytes += *bytes;
            if (map_progress() != before) changed = true;
        }
        else if (line.starts_with("mapfile ")) {
            const std::string_view rest = std::string_view(line).substr(8);
            const auto space = rest.find(' ');
            std::size_t size = 0;
            if (space != std::string_view::npos)
                std::from_chars(rest.data() + space + 1, rest.data() + rest.size(), size);
            if (space == std::string_view::npos || rest.substr(0, space) != m_MapWanted) continue;
            m_MapBytes.clear();
            m_MapReceiving = size > 0 && size <= MapShare::max_size;
            m_MapRefused = !m_MapReceiving;
            m_MapSize = size;
            if (m_MapReceiving) m_MapBytes.reserve(size);
            changed = true;
        }
        else if (line.starts_with("mapend ")) {
            if (!m_MapReceiving || std::string_view(line).substr(7) != m_MapWanted) continue;
            m_MapReceiving = false;
            if (m_MapBytes.size() == m_MapSize && MapShare::fingerprint(m_MapBytes) == m_MapWanted)
                m_MapDone = std::move(m_MapBytes);
            else
                m_MapRefused = true;
            m_MapBytes.clear();
            changed = true;
        }
        else if (line.starts_with("mapno ")) {
            if (std::string_view(line).substr(6) != m_MapWanted) continue;
            m_MapReceiving = false;
            m_MapRefused = true;
            m_MapBytes.clear();
            changed = true;
        }
        else if (line == "loading") {
            m_Phase = Phase::Loading;
            changed = true;
        }
        else if (line.starts_with("rules ")) {
            m_Rules = BattleRules::from_text(std::string_view(line).substr(6));
        }
        else if (line.starts_with("start ")) {
            auto start = NetBattle::parse_start(line);
            if (!start || !m_Rules) {
                fail(Failure::Lost);
                return true;
            }
            start->rules = *m_Rules;
            m_Start = std::move(start);
            m_Phase = Phase::Started;
            return true;
        }
        else if (line == "bye") {
            fail(Failure::HostLeft);
            return true;
        }
        else if (line == "kicked") {
            fail(Failure::Kicked);
            return true;
        }
    }
    // Once connected a client waits for the lobby however long: the host's window may be in the background.
    if (!open && m_Phase != Phase::Started) {
        fail(m_Phase == Phase::Connecting ? Failure::Unreachable : Failure::HostLeft);
        return true;
    }
    return changed;
}

bool Client::ready() const {
    return m_You > 0 && m_You < int(m_State.people.size()) && m_State.people[m_You].ready;
}

void Client::set_bonus(BattleChoice::Bonus bonus) {
    m_Bonus = bonus;
    if (!m_Link || m_Phase != Phase::InLobby) return;
    m_Link->send_line(std::string("bonus ") + BattleRules::bonus_name(bonus));
    m_Link->pump();
}

void Client::set_side(int side) {
    if (!m_Link || m_Phase != Phase::InLobby) return;
    m_Link->send_line(std::format("side {}", side));
    m_Link->pump();
}

void Client::request_map(const std::string &hash) {
    if (hash.empty() || hash == m_MapWanted || !m_Link || m_Phase != Phase::InLobby) return;
    forget_map();
    m_MapWanted = hash;
    m_Link->send_line("mapget " + hash);
    m_Link->pump();
}

int Client::map_progress() const {
    if (!m_MapReceiving || m_MapSize == 0) return -1;
    return int(m_MapBytes.size() * 100 / m_MapSize);
}

std::optional<std::string> Client::take_map() { return std::exchange(m_MapDone, std::nullopt); }

void Client::forget_map() {
    m_MapWanted.clear();
    m_MapBytes.clear();
    m_MapSize = 0;
    m_MapReceiving = false;
    m_MapRefused = false;
    m_MapDone.reset();
}

void Client::set_ready(bool ready) {
    if (!m_Link || m_Phase != Phase::InLobby) return;
    m_Link->send_line(ready ? "ready 1" : "ready 0");
    m_Link->pump();
}

void Client::close() {
    if (m_Link && m_Phase != Phase::Started) {
        m_Link->send_line("bye");
        m_Link->flush(500);
    }
    m_Link.reset();
    m_Connector.reset();
    if (m_Phase != Phase::Started) m_Phase = Phase::Failed;
}

void Client::fail(Failure failure) {
    m_Link.reset();
    m_Connector.reset();
    m_Phase = Phase::Failed;
    m_Failure = failure;
}

}  // namespace NetLobby
