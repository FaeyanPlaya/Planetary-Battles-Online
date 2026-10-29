// MatrixGame - licensed under GPLv2 or any later version.
#include "NetBattle.hpp"

#include "NetLink.hpp"
#include "ExecutablePath.hpp"
#include "BattleChat.hpp"
#include "BattleNotice.hpp"
#include "BattleStats.hpp"
#include "BattleWait.hpp"
#include "WindowDisplay.hpp"
#include "Factions.hpp"
#include "PlayerName.hpp"
#include "GameData.hpp"
#include "MapShare.hpp"
#include "Mods.hpp"
#include "SettingsModel.hpp"
#include "MatrixGame.h"
#include "StringConstants.hpp"
#include "CBlockPar.hpp"
#include "StandaloneRuntime.hpp"
#include "Commands.hpp"
#include "LoopPlatform.hpp"
#include "MatrixMap.hpp"
#include "MatrixLogic.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "Interface/CInterface.h"
#include "SimLab.hpp"
#include "SoundBridge.hpp"
#include "random.hpp"
#include "utils.hpp"
#include "input.hpp"
#include "3g.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <stupid_logger.hpp>
#include <tuple>

namespace NetBattle {
namespace {

constexpr int hash_every_ms = 1000;
constexpr int wait_shown_after_ms = 1500;  // a shorter wait is the network's ordinary jitter
constexpr int drop_after_ms = 60000;       // a person who keeps the battle waiting so long is removed
constexpr auto announce_every = std::chrono::milliseconds(500);
constexpr auto catch_up_slice = std::chrono::milliseconds(100);  // undrawn replay between two pictures
constexpr auto hello_wait = std::chrono::seconds(10);           // host: a connection that says nothing
constexpr auto back_load_wait = std::chrono::minutes(3);        // host: a person coming back loads the map
constexpr int back_connect_ms = 15000;
constexpr const wchar_t *ticket_file = L"CFG\\rejoin.txt";
constexpr long long ticket_lifetime_s = 12 * 3600;

// Connections from the handshake to the end of the battle: the host's clients by peer index,
// or the client's host as peer 0.
std::vector<std::unique_ptr<NetLink::Connection>> g_links;
std::optional<Prepared> g_prepared;
bool g_return_to_menu = false;
bool g_test_pointer = false;  // --net-test-pointer
std::wstring g_restart_arguments;  // the lobby (or battle) to come back to; empty: the startup menu

int parse_int(std::wstring_view text, const char *what) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(std::wstring(text), &used);
        if (used == text.size()) return value;
    }
    catch (const std::exception &) {
    }
    throw std::runtime_error(std::format("Invalid {} on the command line", what));
}

// "<a>,<b>" (test options).
std::pair<int, int> parse_pair(std::wstring_view text, const char *what) {
    const auto comma = text.find(L',');
    if (comma == std::wstring_view::npos) throw std::runtime_error(std::format("{} needs <ms>,<length>", what));
    return {parse_int(text.substr(0, comma), what), parse_int(text.substr(comma + 1), what)};
}

void parse_endpoint(std::wstring_view text, Options &options) {
    const auto colon = text.rfind(L':');
    if (colon != std::wstring_view::npos) {
        options.address = utils::from_wstring(text.substr(0, colon));
        text = text.substr(colon + 1);
    }
    options.port = parse_int(text, "network port");
}

std::string side_list(unsigned sides) {
    std::string out;
    for (int id = 1; id < 32; ++id)
        if (sides & (1u << id)) out += (out.empty() ? "" : ",") + std::to_string(id);
    return out;
}

unsigned parse_sides(std::string_view list) {
    unsigned sides = 0;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const int side = std::atoi(std::string(list.substr(0, comma)).c_str());
        if (side > 0 && side < 32) sides |= 1u << side;
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return sides;
}

// The map the host plays: the command line's, else the one chosen in the map selector.
std::wstring host_map(const Options &options, const StandaloneSettings::Values &settings) {
    std::wstring map = !options.map.empty() ? options.map : settings.map;
    if (map.empty()) map = L"desert1_3e";
    if (!std::filesystem::path(map).has_extension()) map += L".cmap";
    return map;
}

void fail(const std::string &reason) {
    lgr.error("Net: {}")(reason);
    MessageBoxA(nullptr, reason.c_str(), "Planetary Battles Online", MB_OK | MB_ICONWARNING);
}

// The way back to a client's battle (rejoin_ticket): written when it starts.
void save_ticket(const Options &options, const Start &start) {
    std::ofstream out{std::filesystem::path(ticket_file), std::ios::trunc};
    out << std::format("host={}\nport={}\nkey={}\nside={}\nmap={}\ntime={}\n", options.address, options.port, start.key,
                       start.side, utils::from_wstring(std::filesystem::path(start.map).stem().wstring()),
                       static_cast<long long>(std::time(nullptr)));
}

std::wstring rejoin_arguments(const std::string &address, int port, const std::wstring &name) {
    std::wstring arguments = L" --net-rejoin " + utils::to_wstring(std::format("{}:{}", address, port));
    if (!name.empty()) arguments += L" --lobby-name " + name;
    return arguments;
}

// Before the battle's data is loaded the game's language is not known: Windows' is.
bool system_russian() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN; }

struct Battle {
    Options options;
    Start start;
    Lockstep::Session session;
    std::ofstream trace;
    std::ofstream commands;  // battle-commands.txt: this battle as a lab command replay
    std::vector<Commands::Command> bot;
    std::size_t bot_next = 0;
    int last_time = -1;
    std::chrono::steady_clock::time_point clock;
    std::chrono::steady_clock::time_point last_advance;  // for the slow-frame note
    bool host_left_early = false;  // a client's host went before the battle was decided
    int test_turret_building = 0;  // --net-test-turret: the building it ordered turrets at
    int test_drive_robot = 0;      // --net-test-drive: the robot it drives
    int budget_ms = 0;  // game time the real clock allows this iteration
    int steps = 0;      // steps run this iteration
    bool exiting = false;
    std::deque<std::pair<int, std::string>> dumps;  // recent world dumps, for a desync report
    // The battle stream's draws after each step of the last 20 s, for a desync report: the two people's lists show the
    // step where one drew a number the other did not (a world equal in every dumped field can differ only so).
    std::deque<std::pair<int, std::uint64_t>> draws;
    bool desync_shown = false;
    bool watching_shown = false;  // this person lost or gave up and watches on
    bool watched = false;         // watched before the battle was decided
    bool decided = false;         // one side is left, or no person plays any more
    bool end_shown = false;
    unsigned gone_sides = 0;  // sides whose leaving was announced
    // The waiting window.
    using Time = std::chrono::steady_clock::time_point;
    Time stall_since{};                 // this peer could not step since; empty: it steps
    std::vector<Time> blocking_since;   // host: since when each peer keeps the battle waiting
    Time last_announce{};               // host: the last W line
    bool announced = false;             // host: the clients were told of a wait not yet over
    std::vector<std::pair<int, int>> host_waits;  // client: the host's latest W line
    Time waits_at{};                    // client: when it came
    bool swallow_up = false;            // the button press was the window's: so is the release
    bool device_lost = false;           // noted: the device is lost (the battle goes on undrawn)
    // Coming back after a lost connection.
    struct Back {
        std::unique_ptr<NetLink::Connection> link;
        int peer = -1;  // the person's peer once their key was taken; -1: no hello yet
        Time since;
    };
    std::unique_ptr<NetLink::Listener> listener;  // host: people coming back
    std::vector<Back> coming_back;                 // host: their new connections, until loaded
    std::array<std::string, BattleRules::side_count> keys;  // host: each client's key, by side
    bool catching = false;          // client: replaying the battle to come back into it
    bool muted = false;             // the sound is off while replaying
    Time catch_frame{};             // when this iteration's replay began
    bool host_lost_shown = false;   // client: the window of a lost host is up
    bool host_lost_closed = false;  // client: the person chose to play on alone
    bool rejoin_restart = false;    // the game starts again to come back to this battle

    Battle(const Options &o, const Start &s, Lockstep::Session &&session)
        : options(o), start(s), session(std::move(session)) {}
};

bool russian() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
           SettingsModel::Language::Russian;
}

void update_title(Battle &b) {
    // "Planetary Battles Online - host, side 1" in Russian, with the latest note.
    std::wstring title = L"Planetary Battles Online \u2014 ";
    title += b.session.is_host() ? L"\u0445\u043e\u0441\u0442" : L"\u043a\u043b\u0438\u0435\u043d\u0442";
    title += std::format(L", \u0441\u0442\u043e\u0440\u043e\u043d\u0430 {}", b.start.side);
    if (!b.session.started()) title += L" \u2014 \u0436\u0434\u0451\u043c \u0438\u0433\u0440\u043e\u043a\u043e\u0432";
    SetWindowTextW(g_Wnd, title.c_str());
}

void note(Battle &b, const std::string &event) {
    lgr.warning("Net: {}")(event);
    if (b.trace.is_open()) {
        b.trace << std::format("event t={} {}\n", g_MatrixMap->GetTime(), event);
        b.trace.flush();
    }
}

// Next to the trace, or in a played battle next to test.log: comparing the two people's files shows
// which objects differ.
void write_desync_dump(Battle &b, int time) {
    const std::wstring path = b.options.trace.empty()
                                      ? std::format(L"desync-{}s-side{}.txt", time / 1000, b.start.side)
                                      : b.options.trace + std::format(L".desync-{}.txt", time);
    for (const auto &[t, dump] : b.dumps) {
        if (t != time) continue;
        std::ofstream out{std::filesystem::path(path)};
        out << std::format("side {} t={} draws={}\n", b.start.side, time, [&] {
            for (const auto &[at, n] : b.draws)
                if (at == time) return std::to_string(n);
            return std::string("?");
        }()) << dump;
        // Each step's draws where they changed (the earlier ones, then every change up to the report).
        out << "draws after each step (t count):\n";
        std::uint64_t last = ~std::uint64_t{0};
        for (const auto &[at, n] : b.draws) {
            if (n == last) continue;
            out << std::format("  {} {}\n", at, n);
            last = n;
        }
        note(b, "desync: world written to " + utils::from_wstring(path));
    }
}

void route(Battle &b) {
    for (Lockstep::Outgoing &o : b.session.take_outgoing()) {
        for (std::size_t i = 0; i < g_links.size(); ++i)
            if (g_links[i] && (o.peer < 0 || o.peer == int(i))) g_links[i]->send_line(o.line);
    }
}

std::wstring side_name(const Battle &b, int side) {
    return side >= 1 && side <= BattleRules::side_count ? b.start.names[std::size_t(side - 1)] : std::wstring();
}

// "Player Rover (Keller)"; without a name, the faction alone.
std::wstring person_of(const Battle &b, int side, bool ru) {
    const std::wstring &name = b.start.names[std::size_t(side - 1) % b.start.names.size()];
    return name.empty() ? std::wstring(Factions::name(side, ru))
                        : (ru ? L"Игрок " : L"Player ") + name + L" (" + Factions::name(side, ru) + L")";
}

void refuse(NetLink::Connection &link, const std::string &reason) {
    link.send_line("refused " + reason);
    link.flush(500);
}

// Host: the first line of a connection made during the battle. Only a person who lost the connection may come
// back, with the key their start line gave them; they get the battle's start again and load it. False: refused.
bool take_back(Battle &b, Battle::Back &back, const std::string &line) {
    NetLink::Connection &link = *back.link;
    const auto hello = Lobby::parse_hello(line);
    if (!hello) {
        note(b, "a connection did not introduce itself: " + line);
        return false;
    }
    if (hello->version != Lobby::protocol_version) {
        refuse(link, std::format("version {}", Lobby::protocol_version));
        return false;
    }
    if (hello->build != GameData::build_fingerprint() || hello->data != GameData::data_fingerprint()) {
        refuse(link, hello->build != GameData::build_fingerprint() ? "build" : "data");
        return false;
    }
    // A person gone from the battle with this name (they may have lost the way back: a new start of the game
    // elsewhere, a crash that took the ticket); the name stands for the key only while their side is free to take.
    int named = -1;
    for (int i = 0; i < b.session.peer_count(); ++i) {
        const int side = b.session.peer_side(i);
        if (side >= 1 && side <= BattleRules::side_count && !hello->name.empty() &&
            PlayerName::same(b.start.names[std::size_t(side - 1)], hello->name) && !b.session.peer_live(i) &&
            b.session.can_rejoin(i) && !b.decided && g_MatrixMap->GetSideById(side)->GetStatus() == SS_ACTIVE)
            named = i;
    }
    if (hello->rejoin.empty()) {
        // Joining the lobby: it is a battle now. "back": they may come back to it (the lobby window does so).
        refuse(link, named >= 0 ? "started back" : "started");
        return false;
    }
    int peer = -1;
    for (int i = 0; i < b.session.peer_count(); ++i) {
        const int side = b.session.peer_side(i);
        if (side >= 1 && side <= BattleRules::side_count && b.keys[side - 1] == hello->rejoin) peer = i;
    }
    if (peer < 0) peer = named;  // no key, or one of another battle
    const int side = b.session.peer_side(peer);
    const char *why = peer < 0 ? "unknown" : b.decided ? "over" : nullptr;
    if (!why && b.session.peer_live(peer)) {
        // The person is back on a new connection before the old one failed here: it has.
        note(b, std::format("side {} comes back while its old connection seemed open", side));
        b.session.lost(peer);
        g_links[std::size_t(peer)].reset();
    }
    if (!why && !b.session.can_rejoin(peer)) why = "left";
    if (!why && g_MatrixMap->GetSideById(side)->GetStatus() != SS_ACTIVE) why = "beaten";
    if (why) {
        refuse(link, std::string("rejoin ") + why);
        note(b, std::format("refused a person coming back: {}", why));
        return false;
    }
    for (Battle::Back &other : b.coming_back)
        if (&other != &back && other.peer == peer) other.peer = -2;  // an older attempt: dropped
    Start theirs = b.start;
    theirs.side = side;
    theirs.key = b.keys[side - 1];
    link.send_line("rules " + BattleRules::to_text(b.start.rules));
    link.send_line(start_line(theirs));
    link.pump();
    back.peer = peer;
    note(b, std::format("side {} is coming back: loading the battle", side));
    const bool ru = russian();
    BattleNotice::show(person_of(b, side, ru) + (ru ? L" возвращается в бой" : L" is coming back"));
    return true;
}

// Host: people coming back. Once loaded (ready) a person's connection is the peer's again and gets the battle's
// turns so far; the battle does not wait for them until they have replayed it.
void poll_coming_back(Battle &b) {
    if (!b.listener) return;
    const auto t = std::chrono::steady_clock::now();
    while (auto link = b.listener->accept(0)) b.coming_back.push_back({std::move(link), -1, t});
    for (std::size_t i = 0; i < b.coming_back.size();) {
        Battle::Back &back = b.coming_back[i];
        const bool open = back.link->pump();
        bool done = back.peer == -2;
        std::string line;
        while (!done && back.link->next_line(line)) {
            if (back.peer < 0) {
                done = !take_back(b, back, line);
            }
            else if (line == "ready") {
                const int peer = back.peer;
                g_links[std::size_t(peer)] = std::move(back.link);
                b.session.rejoin(peer);
                done = true;
            }
            else if (line == "bye") {
                done = true;
            }
        }
        if (done || !open || t - back.since > (back.peer < 0 ? std::chrono::duration_cast<std::chrono::milliseconds>(hello_wait)
                                                              : std::chrono::duration_cast<std::chrono::milliseconds>(back_load_wait))) {
            b.coming_back.erase(b.coming_back.begin() + std::ptrdiff_t(i));
            continue;
        }
        ++i;
    }
}

void pump(Battle &b) {
    poll_coming_back(b);
    const bool was_started = b.session.started();
    for (int pass = 0; pass < 2; ++pass) {  // received turns can make the host send at once
        route(b);
        for (std::size_t i = 0; i < g_links.size(); ++i) {
            auto &link = g_links[i];
            if (!link) continue;
            const bool open = link->pump();
            std::string line;
            while (link->next_line(line)) b.session.receive(int(i), line);
            if (!open) {
                b.session.lost(int(i));
                link.reset();
            }
        }
    }
    route(b);
    for (std::size_t i = 0; i < g_links.size(); ++i) {
        auto &link = g_links[i];
        if (!link) continue;
        link->pump();
        if (!b.session.peer_live(int(i))) {  // removed, or the host removed this person
            link->flush(500);
            link.reset();
        }
    }
    for (Commands::Command &c : b.session.take_scheduled()) Commands::schedule(std::move(c));
    for (const auto &[side, text] : b.session.take_chat()) {
        std::wstring wide;
        try {
            wide = utils::to_wstring(text);
        }
        catch (...) {
            continue;
        }
        note(b, std::format("chat side {}: {}", side, text));
        BattleChat::show(side, side_name(b, side), wide, russian());
    }
    bool title = b.session.started() != was_started;
    for (const std::string &event : b.session.take_events()) {
        note(b, event);
        title = true;
    }
    if (const int t = b.session.first_desync(); t >= 0 && !b.desync_shown) {
        b.desync_shown = true;
        BattleNotice::show(std::format(L"{} (t={} {})", russian() ? L"\u0420\u0430\u0441\u0441\u0438\u043d\u0445\u0440\u043e\u043d\u0438\u0437\u0430\u0446\u0438\u044f!" : L"Desync!",
                                       t / 1000, russian() ? L"\u0441" : L"s"),
                           0xFFFF6060);
        write_desync_dump(b, t);
        title = true;
    }
    if (g_MatrixMap->IsSpectating() && !b.watching_shown) {
        b.watching_shown = true;
        BattleNotice::show(russian() ? L"\u0412\u044b \u043d\u0430\u0431\u043b\u044e\u0434\u0430\u0435\u0442\u0435 \u0437\u0430 \u0431\u043e\u0435\u043c. \u0412\u044b\u0439\u0442\u0438: \u041c\u0435\u043d\u044e, \u0412\u044b\u0445\u043e\u0434"
                                     : L"You are watching the battle. To leave: Menu, Exit");
    }
    if (title) update_title(b);
}

// Test: this person points a new turret at one of its buildings with a free turret place, as the build
// buttons do, or builds it at the first free place as a click does.
void test_turret(Battle &b, bool open) {
    CMatrixSideUnit *side = g_MatrixMap->GetPlayerSide();
    if (!open) {
        CMatrixBuilding *base = side->m_CannonForBuild.m_ParentBuilding;
        side->m_CannonForBuild.Delete();
        side->m_CurrentAction = NOTHING_SPECIAL;
        CPoint places[MAX_PLACES];
        if (!base || base->GetPlacesForTurrets(places) < 1) {
            note(b, std::format("test: turret put away (no free place; places {}, turrets {} of {})", base ? base->m_TurretsPlacesCnt : -1, base ? base->m_TurretsHave : -1, base ? base->m_TurretsMax : -1));
            return;
        }
        float angle = 0;
        for (int i = 0; i < base->m_TurretsPlacesCnt; ++i)
            if (base->m_TurretsPlaces[i].m_Coord == places[0]) angle = float(base->m_TurretsPlaces[i].m_Angle);
        Commands::Command command;
        command.side = side->m_Id;
        command.type = Commands::Type::BuildTurret;
        command.x = places[0].x * GLOBAL_SCALE_MOVE;
        command.y = places[0].y * GLOBAL_SCALE_MOVE;
        command.target = base->GetSimId();
        command.params = {1, g_MatrixMap->m_RN.FindInPL(places[0]), std::bit_cast<int>(angle)};
        // Twice, as a quick second click on the place still green: only one turret may come of it.
        Commands::issue(command);
        Commands::issue(command);
        b.test_turret_building = base->GetSimId();
        note(b, std::format("test: turret ordered twice at building {} (turrets {})", base->GetSimId(), base->m_TurretsHave));
        return;
    }
    for (CMatrixMapStatic *ms = CMatrixMapStatic::GetFirstLogic(); ms; ms = ms->GetNextLogic())
        if (CPoint places[MAX_PLACES]; ms->IsLiveBuilding() && ms->GetSide() == side->m_Id && ms->AsBuilding()->GetPlacesForTurrets(places) > 0) {
            side->m_ActiveObject = ms;
            side->m_CurrSel = BASE_SELECTED;
            const auto began = std::chrono::steady_clock::now();
            g_IFaceList->BeginBuildTurret(1);
            if (side->m_CannonForBuild.m_Cannon) side->m_CannonForBuild.m_Cannon->RNeed(MR_Matrix | MR_Graph);
            const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
            // What the preview costs every step: its search of free places.
            const auto search = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i) ms->AsBuilding()->GetPlacesForTurrets(places);
            const auto each = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - search) / 100;
            note(b, std::format("test: turret pointed at base {} ({} ms to load it, {} us a place search)", ms->GetSimId(),
                                took.count(), each.count()));
            return;
        }
    note(b, "test: no building with a free turret place");
}

// Test: this person enters its first robot, drives it in a fixed pattern through the same commands as the
// keys (forward, forward turning left, firing ahead while turning right, back) and leaves it.
void test_drive(Battle &b, int now) {
    CMatrixSideUnit *side = g_MatrixMap->GetPlayerSide();
    const int start = b.options.test_drive_ms, end = start + b.options.test_drive_length;
    if (now == start) {
        for (CMatrixMapStatic *ms = CMatrixMapStatic::GetFirstLogic(); ms; ms = ms->GetNextLogic())
            if (ms->IsLiveRobot() && ms->GetSide() == side->m_Id && !ms->AsRobot()->IsDisableManual() &&
                ms->AsRobot()->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) {
                b.test_drive_robot = ms->GetSimId();
                Commands::set_interface_input(false);  // the pattern, not this PC's keyboard
                side->RequestDrive(Commands::DriveAction::Enter, ms);
                note(b, std::format("test: entering robot {} (max speed {:.3f})", ms->GetSimId(), ms->AsRobot()->GetMaxSpeed()));
                return;
            }
        note(b, "test: no robot to drive");
        return;
    }
    if (!b.test_drive_robot || now < start || now > end + 2000) return;
    CMatrixMapStatic *robot = Commands::find(b.test_drive_robot);
    if (now == end) {
        side->RequestDrive(Commands::DriveAction::Leave, robot);
        Commands::set_interface_input(true);
        note(b, "test: leaving the robot");
        return;
    }
    if (now == end + 2000) {
        note(b, std::format("test: after leaving, in a robot: {}", side->IsArcadeMode() ? "yes" : "no"));
        return;
    }
    if (now > end || !robot || !robot->IsLiveRobot() || side->GetArcadedObject() != robot) {
        if (now % 1000 == 0) note(b, std::format("test: not in the robot at {} ms", now));
        return;
    }
    CMatrixRobotAI *r = robot->AsRobot();
    using namespace Commands;
    const int phase = (now - start) / 1000 % 4;
    const int keys = DRIVE_AIMING | (phase == 0   ? DRIVE_FORWARD
                                     : phase == 1 ? DRIVE_FORWARD | DRIVE_LEFT
                                     : phase == 2 ? DRIVE_FIRE | DRIVE_RIGHT
                                                  : DRIVE_BACKWARD);
    const D3DXVECTOR3 aim(r->m_PosX + r->m_Forward.x * 150, r->m_PosY + r->m_Forward.y * 150, r->GetGeoCenter().z);
    side->IssueDrive(keys, aim);
    if ((now - start) % 1000 == 500)
        note(b, std::format("test: driving robot {} at {:.0f},{:.0f} (phase {}, max speed {:.3f})", r->GetSimId(), r->m_PosX,
                            r->m_PosY, phase, r->GetMaxSpeed()));
}

// The first turret of a kind loaded its model and skins from the packages in the middle of the battle:
// on a slow PC that froze the battle for everyone. Every kind is loaded before the first step instead
// (the cache keeps them); a cannon outside the map's lists changes nothing in the battle.
void preload_turrets(Battle &b) {
    const auto began = std::chrono::steady_clock::now();
    for (int kind = 1; kind <= CANNON_TYPE_CNT; ++kind) {
        CMatrixCannon *cannon = HNew(g_MatrixHeap) CMatrixCannon;
        cannon->SetSide(PLAYER_SIDE);
        cannon->UnitInit(kind);
        cannon->RNeed(MR_Matrix | MR_Graph);
        HDelete(CMatrixCannon, cannon, g_MatrixHeap);
    }
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began);
    note(b, std::format("turret models loaded in {} ms", took.count()));
}

// Bookkeeping after a step that brought the game time to `now` (and once at the start).
void stepped(Battle &b, int now) {
    b.session.stepped(now);
    b.draws.emplace_back(now, random::draw_count());
    if (b.draws.size() > 2000) b.draws.pop_front();
    if (now % hash_every_ms == 0) {
        std::string dump;
        // Replaying, the world's text is wanted only near the host's present: older hashes are not compared.
        const bool old = b.catching && now < (b.session.history_turn() - 100) * b.start.config.turn_ms &&
                         now != b.options.test_dump_ms;
        const std::uint64_t hash = SimLab::world_hash(old ? nullptr : &dump);
        if (now == b.options.test_dump_ms && !b.options.trace.empty()) {
            std::ofstream{std::filesystem::path(b.options.trace + std::format(L".dump-{}.txt", now))}
                << std::format("side {} t={}\n", b.start.side, now) << dump;
        }
        b.dumps.emplace_back(now, std::move(dump));
        if (b.dumps.size() > 8) b.dumps.pop_front();
        if (b.trace.is_open()) b.trace << std::format("H {} {:016x} draws={}\n", now, hash, random::draw_count());
        b.session.hashed(now, hash);
    }
    while (b.bot_next < b.bot.size() && b.bot[b.bot_next].time <= now) Commands::issue(b.bot[b.bot_next++]);
    if (now == b.options.test_crash_ms) {
        note(b, "test: the process ends here without a word");
        if (b.commands.is_open()) b.commands.flush();
        TerminateProcess(GetCurrentProcess(), 3);
    }
    if (b.catching) return;  // replaying: the tests below already ran in the first life of this battle
    if (now == b.options.test_desync_ms) random::Rnd();
    if (now == b.options.test_lost_ms) CMatrixMap::m_TestDeviceLost = true;
    if (b.options.test_lost_ms >= 0 && now == b.options.test_lost_ms + b.options.test_lost_length)
        CMatrixMap::m_TestDeviceLost = false;
    if (b.options.test_drive_ms >= 0) test_drive(b, now);
    if (now == b.options.test_turret_ms) test_turret(b, true);
    if (b.options.test_turret_ms >= 0 && now == b.options.test_turret_ms + b.options.test_turret_length)
        test_turret(b, false);
    if (b.test_turret_building && now == b.options.test_turret_ms + b.options.test_turret_length + 2000)
        for (CMatrixMapStatic *ms = CMatrixMapStatic::GetFirstLogic(); ms; ms = ms->GetNextLogic())
            if (ms->GetSimId() == b.test_turret_building && ms->IsBuilding())
                note(b, std::format("test: building {} has {} turrets", b.test_turret_building, ms->AsBuilding()->m_TurretsHave));
    if (now == b.options.test_hang_ms) {
        note(b, std::format("test: not answering for {} ms", b.options.test_hang_length));
        Sleep(DWORD(b.options.test_hang_length));  // no messages, no lines: as a hung or dragged window
    }
    if (b.options.exit_ms >= 0 && now >= b.options.exit_ms && !b.exiting) {
        b.exiting = true;
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
}

Loop::Clock::time_point loop_now(void *context) {
    Battle &b = *static_cast<Battle *>(context);
    // While the device is lost nothing is drawn: frames would follow each other without a pause.
    if (CMatrixMap::m_DeviceLost) MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    const auto now = std::chrono::steady_clock::now();
    // Whole milliseconds only, the rest carried over: frames can be shorter than a millisecond.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - b.clock).count();
    b.clock += std::chrono::milliseconds(elapsed);
    // A long stall (window dragged, peers waiting) is not made up for at once.
    b.budget_ms = std::min<int>(b.budget_ms + int(std::min<long long>(elapsed, 250)), 250);
    b.steps = 0;
    b.catch_frame = now;
    return Loop::Clock::now();
}

bool loop_poll(void *, MSG &message) {
    // The battle goes on for everyone while this window is in the background.
    SETFLAG(g_Flags, GFLAG_APPACTIVE);
    return PeekMessage(&message, NULL, 0, 0, PM_REMOVE) != FALSE;
}

// The window after this person lost the battle's connection: come back (the game starts again and replays the
// battle) or leave; with the host lost, play on alone.
void choose_after_loss(Battle &b, bool back) {
    if (back) {
        note(b, "the person comes back: the game starts again");
        b.rejoin_restart = true;
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
    else if (b.session.dropped()) {
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
    else {
        note(b, "the person plays on alone");
        b.host_lost_closed = true;
        b.host_lost_shown = false;
        BattleWait::hide();
    }
}

// Chat: Yo or a free Enter opens the line (not over a dialog or the waiting window), keys type into it, Enter sends,
// Esc drops it. True: the message was the chat's (the battle's interface does not see it).
bool chat_key(Battle &b, const MSG &m) {
    if (BattleChat::typing()) {
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_RETURN) {
                const std::wstring text = BattleChat::finish();
                if (!text.empty()) {
                    b.session.say(utils::from_wstring(text));
                    BattleChat::show(b.start.side, side_name(b, b.start.side), text, russian());
                }
            }
            else if (m.wParam == VK_ESCAPE) {
                BattleChat::close();
            }
            else if (m.wParam == VK_BACK) {
                BattleChat::erase_last();
            }
            else if (m.wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                if (OpenClipboard(g_Wnd)) {
                    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
                        if (const auto *text = static_cast<const wchar_t *>(GlobalLock(data))) {
                            BattleChat::paste(std::wstring_view(text).substr(0, BattleChat::max_length));
                            GlobalUnlock(data);
                        }
                    CloseClipboard();
                }
            }
            else {
                // The key in the keyboard's present layout, as Unicode: the game's window is not a Unicode one, so its
                // WM_CHAR would come in the system's code page (Cyrillic lost where that is not 1251).
                BYTE keys[256]{};
                wchar_t typed[8]{};
                if (GetKeyboardState(keys)) {
                    const int n = ToUnicodeEx(UINT(m.wParam), UINT((m.lParam >> 16) & 0xff), keys, typed, 8, 0,
                                              GetKeyboardLayout(0));
                    for (int i = 0; i < n; ++i) BattleChat::type(typed[i]);  // (n < 0: a dead key, waiting for the next)
                }
            }
            return true;
        }
        return m.message == WM_CHAR || m.message == WM_SYSCHAR;  // (none come: nothing translates the keys)
    }
    const bool repeat = (m.lParam & (1 << 30)) != 0;
    // Yo (`) always; Enter when it has no other work (it enters the selected robot and leaves the driven one).
    CMatrixSideUnit *own = g_MatrixMap->GetPlayerSide();
    const bool enter_free = !own->IsRobotMode() &&
                            !(own->GetCurGroup() && (own->m_CurrSel == ROBOT_SELECTED || own->m_CurrSel == GROUP_SELECTED));
    const bool opens = m.wParam == VK_OEM_3 || (m.wParam == VK_RETURN && enter_free);
    if (m.message == WM_KEYDOWN && opens && !repeat && !FLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE) &&
        !g_MatrixMap->m_DialogModeName && !BattleWait::shown() && !b.catching && !b.session.dropped() && !b.session.alone()) {
        BattleChat::open(russian());
        Input::onFocusLost();  // keys held now (a robot's) are let go
        return true;
    }
    return false;
}

void loop_dispatch(void *context, const MSG &message) {
    Battle &b = *static_cast<Battle *>(context);
    // The interface only issues commands; randoms it draws must not touch the battle's stream.
    random::CosmeticScope interface_only;
    if (message.hwnd == g_Wnd && chat_key(b, message)) return;
    if (BattleWait::shown() && message.hwnd == g_Wnd) {
        int x = static_cast<int16_t>(LOWORD(message.lParam)), y = static_cast<int16_t>(HIWORD(message.lParam));
        if (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) Display::map_mouse(message.message, x, y);
        if (message.message == WM_MOUSEMOVE) BattleWait::hover(x, y);
        if (message.message == WM_LBUTTONDOWN) {
            if (const int button = BattleWait::button_at(x, y); button >= 0) {
                b.swallow_up = true;
                if (b.session.dropped() != 0 || b.host_lost_shown) {
                    choose_after_loss(b, button == 0);
                }
                else if (b.session.is_host()) {
                    // Go on without them: the side passes to the AI at once, as when they lose the connection; they may come back.
                    note(b, std::format("the host removes side {}", b.session.peer_side(button)));
                    b.session.drop(button, true);
                }
                return;
            }
        }
        if (message.message == WM_LBUTTONUP && b.swallow_up) {
            b.swallow_up = false;
            return;
        }
        if ((b.session.dropped() != 0 || b.host_lost_shown) && message.message == WM_KEYDOWN &&
            (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
            choose_after_loss(b, message.wParam == VK_RETURN);
            return;
        }
    }
    DispatchMessage(&message);
}

void loop_wait(void *) { MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT); }

int ms_between(Battle::Time from, Battle::Time to) {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
}

BattleWait::Person waited_person(const Battle &b, int side, int ms) {
    BattleWait::Person p;
    p.side = side;
    p.name = b.start.names[std::size_t(side - 1) % b.start.names.size()];
    p.waited_ms = ms;
    p.loading = !b.session.started();
    return p;
}

// Whom the battle waits for, in the waiting window: the host measures every peer and tells the clients
// (W lines); a client whose host says nothing shows the host. A peer keeping the battle waiting for
// drop_after_ms is removed (the host's side, for a client: it plays on alone).
void watch_waits(Battle &b, int now) {
    const auto t = std::chrono::steady_clock::now();
    const bool ru = russian();
    const auto note_buttons = [&b] {
        // Where the buttons are, for tests that press them.
        if (!b.trace.is_open()) return;
        std::string where;
        for (const auto &r : BattleWait::button_rects()) where += std::format(" {},{},{},{}", r[0], r[1], r[2], r[3]);
        note(b, std::format("waiting window; screen {}x{}; buttons{}", g_ScreenX, g_ScreenY, where));
    };
    if (b.session.dropped()) {
        BattleWait::Window w;
        w.dropped = b.session.dropped();
        w.dropped_ai = BattleRules::dropped_ai();
        const bool first = !BattleWait::shown();
        BattleWait::show(w, ru);
        if (first) note_buttons();
        return;
    }
    // The host's connection failed (not a goodbye) before the battle was decided: the host may still be there.
    if (!b.session.is_host() && !b.session.peer_live(0) && b.session.peer_how_gone(0) == 2 && !b.decided &&
        !b.host_lost_closed) {
        BattleWait::Window w;
        w.host_lost = true;
        const bool first = !BattleWait::shown();
        BattleWait::show(w, ru);
        if (first) note_buttons();
        b.host_lost_shown = true;
        return;
    }
    const bool stalled = !b.session.started() || !b.session.can_step(now);
    if (!stalled) b.stall_since = {};
    else if (b.stall_since == Battle::Time{}) b.stall_since = t;
    BattleWait::Window w;
    if (b.session.is_host()) {
        const std::vector<int> peers = b.session.waiting_for(now);
        b.blocking_since.resize(std::size_t(b.session.peer_count()));
        std::vector<std::pair<int, int>> waits;
        int longest = 0;
        for (int i = 0; i < b.session.peer_count(); ++i) {
            Battle::Time &since = b.blocking_since[std::size_t(i)];
            if (std::find(peers.begin(), peers.end(), i) == peers.end()) {
                since = {};
                continue;
            }
            if (since == Battle::Time{}) since = t;
            const int ms = ms_between(since, t);
            if (ms < wait_shown_after_ms) continue;
            if (b.session.started() && ms >= drop_after_ms) {
                note(b, std::format("side {} did not answer for {} s: removed", b.session.peer_side(i), ms / 1000));
                b.session.drop(i, false);
                continue;
            }
            if (b.session.started() && b.options.test_remove_ms > 0 && ms >= b.options.test_remove_ms) {
                note(b, std::format("test: the host goes on without side {}", b.session.peer_side(i)));
                b.session.drop(i, true);  // as "Go on without" does
                continue;
            }
            waits.emplace_back(b.session.peer_side(i), ms);
            BattleWait::Person p = waited_person(b, b.session.peer_side(i), ms);
            p.peer = i;
            w.people.push_back(std::move(p));
            longest = std::max(longest, ms);
        }
        if ((!waits.empty() && t - b.last_announce >= announce_every) || (waits.empty() && b.announced)) {
            b.session.announce_waits(waits);
            b.announced = !waits.empty();
            b.last_announce = t;
        }
        w.can_remove = true;
        w.dropped_ai = BattleRules::dropped_ai();
        if (b.session.started() && !w.people.empty()) w.drop_in_ms = drop_after_ms - longest;
    }
    else {
        if (auto waits = b.session.take_waits()) {
            // Not this person: lines the host sent while this window did not answer arrive late.
            std::erase_if(*waits, [&](const auto &w) { return w.first == b.start.side; });
            b.host_waits = std::move(*waits);
            b.waits_at = t;
        }
        // The host's word is fresh while its W lines keep coming.
        const bool told = b.waits_at != Battle::Time{} && t - b.waits_at < std::chrono::seconds(2) && !b.host_waits.empty();
        if (stalled && told) {
            int longest = 0;
            for (const auto &[side, ms] : b.host_waits) {
                w.people.push_back(waited_person(b, side, ms));
                longest = std::max(longest, ms);
            }
            if (b.session.started()) w.drop_in_ms = drop_after_ms - longest;
        }
        else if (stalled && b.session.started()) {
            // The host itself says nothing: since the stall began or its last word, whichever is later.
            const Battle::Time since = std::max(b.stall_since, b.waits_at);
            const int ms = ms_between(since, t);
            if (ms >= drop_after_ms) {
                note(b, std::format("the host did not answer for {} s: playing on alone", ms / 1000));
                b.session.lost(0);
            }
            else if (ms >= wait_shown_after_ms) {
                BattleWait::Person p = waited_person(b, b.start.host_side, ms);
                p.host = true;
                w.people.push_back(std::move(p));
                w.drop_in_ms = drop_after_ms - ms;
            }
        }
    }
    if (w.people.empty()) {
        if (BattleWait::shown()) BattleWait::hide();
        return;
    }
    const bool first = !BattleWait::shown();
    BattleWait::show(w, ru);
    if (first) note_buttons();
}

// The battle is over for its people: one side is left, or no side a person plays is.
bool battle_decided() {
    int active = 0, people = 0;
    for (int i = 0; i < g_MatrixMap->m_SideCnt; ++i) {
        const CMatrixSideUnit &side = g_MatrixMap->m_Side[i];
        if (side.m_Id < 1 || side.GetStatus() != SS_ACTIVE) continue;
        ++active;
        if (g_MatrixMap->IsHumanSide(side.m_Id) && !g_MatrixMap->IsAbsentSide(side.m_Id)) ++people;  // (away: nobody plays it)
    }
    return active <= 1 || people == 0;
}

// A person who watches (lost or gave up) sees the final statistics once the battle is decided; closing
// them ends the battle for them (back to the lobby). The winner gets there through the result dialog.
void watch_the_end(Battle &b) {
    if (!b.decided && battle_decided()) {
        b.decided = true;
        note(b, "the battle is decided");
    }
    if (!g_MatrixMap->IsSpectating()) return;
    if (!b.decided) {
        b.watched = true;
        return;
    }
    if (b.end_shown) return;
    b.end_shown = true;
    if (!b.watched && (g_ExitState == 2 || g_ExitState == 4)) {
        SETFLAG(g_Flags, GFLAG_EXITLOOP);  // this person's result and statistics have just been closed
        return;
    }
    g_ExitState = 5;  // the battle's end: the statistics' OK leaves (CMatrixMap::CloseStatistics)
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG);
}

// A person back in the battle has replayed it (or lost the host meanwhile): the battle shows and sounds again.
void finish_catching(Battle &b, int now) {
    b.catching = false;
    CMatrixMap::m_CatchingUp = false;
    if (b.muted) SoundBridge::master_volume(float(StandaloneSettings::current().sound_volume) / 100.0f);
    b.muted = false;
    BattleWait::hide();
    b.budget_ms = 0;
    b.clock = std::chrono::steady_clock::now();
    // The replay's undrawn steps were not a lost device: drawn from now on (pictures of robots made meanwhile
    // are made when first shown, as after a lost device).
    CMatrixMap::m_DeviceLost = false;
    b.device_lost = false;
    note(b, std::format("replayed the battle to t={}", now));
}

// Coming back: steps as fast as this PC can, undrawn, a slice of real time between two pictures of the progress
// window, until the host's history is replayed; then the host is told and the battle goes on at its pace.
bool catch_up(Battle &b, int now) {
    if (!b.session.peer_live(0)) {
        finish_catching(b, now);
        return false;
    }
    const bool can = b.session.can_step(now);
    if (can && std::chrono::steady_clock::now() - b.catch_frame < catch_up_slice) {
        ++b.steps;
        return true;
    }
    const int history = b.session.history_turn();
    if (!can && history >= 0 && b.session.known_turn() >= history) {
        b.session.caught();
        finish_catching(b, now);
        return false;
    }
    BattleWait::Window w;
    w.catching_up = history > 0 ? std::clamp(int(std::int64_t(now) * 100 / (std::int64_t(history) * b.start.config.turn_ms)), 0, 99) : 0;
    BattleWait::show(w, russian());
    return false;
}

bool loop_advance(void *context) {
    Battle &b = *static_cast<Battle *>(context);
    BattleNotice::update();
    BattleChat::update();
    watch_the_end(b);
    if (b.options.role == Role::Client && !b.decided && b.session.alone()) b.host_left_early = true;
    const int now = g_MatrixMap->GetTime();
    // A long frame here stalls everyone: note it for lag reports.
    const auto real = std::chrono::steady_clock::now();
    if (const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(real - b.last_advance).count();
        b.last_time >= 0 && gap >= 150 && !b.catching)
        note(b, std::format("slow frame: {} ms before t={}", gap, now));
    b.last_advance = real;
    if (now != b.last_time) {
        b.last_time = now;
        stepped(b, now);
    }
    pump(b);
    if (b.catching) {
        if (b.exiting) return false;
        return catch_up(b, now);
    }
    watch_waits(b, now);
    // Removing a person there completes turns at once (their side's Surrender among the commands): those are due
    // before the next step, as pump's are, or this peer would run them a step after everyone else.
    for (Commands::Command &c : b.session.take_scheduled()) Commands::schedule(std::move(c));
    if (const bool lost = CMatrixMap::m_DeviceLost; lost != b.device_lost) {
        b.device_lost = lost;
        note(b, lost ? "the device is lost; the battle goes on undrawn" : "the device is back");
    }
    if (b.exiting) return false;
    const int step = b.start.config.step_ms;
    const int most = b.options.fast ? 10 : 25;
    if (b.steps >= most || (!b.options.fast && b.budget_ms < step)) return false;
    if (!b.session.can_step(now)) {
        // Waiting for a peer: the time lost is not made up for later beyond one turn.
        b.budget_ms = std::min(b.budget_ms, b.start.config.turn_ms);
        return false;
    }
    b.budget_ms -= step;
    ++b.steps;
    return true;
}

std::vector<Commands::Command> load_bot(const std::wstring &path, int side) {
    std::vector<Commands::Command> bot;
    std::ifstream in{std::filesystem::path(path)};
    if (!in) throw std::runtime_error("Cannot read the bot file " + utils::from_wstring(path));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.starts_with("C ")) continue;
        Commands::Command c;
        if (!Commands::from_line(line, c)) throw std::runtime_error("Bad bot line: " + line);
        if (c.side == side) bot.push_back(std::move(c));
    }
    std::stable_sort(bot.begin(), bot.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
    return bot;
}

}  // namespace

Options parse(std::vector<std::wstring_view> &arguments) {
    Options o;
    std::vector<std::wstring_view> rest;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::wstring_view a = arguments[i];
        if (!a.starts_with(L"--net-") && !a.starts_with(L"--lobby-")) {
            rest.push_back(a);
            continue;
        }
        const auto value = [&]() -> std::wstring_view {
            if (i + 1 >= arguments.size())
                throw std::runtime_error(std::format("{} needs a value", utils::from_wstring(a)));
            return arguments[++i];
        };
        if (a == L"--net-host") {
            o.role = Role::Host;
            parse_endpoint(value(), o);
        }
        else if (a == L"--net-join") {
            o.role = Role::Client;
            parse_endpoint(value(), o);
        }
        else if (a == L"--net-map") o.map = value();
        else if (a == L"--net-seed") o.seed = static_cast<std::uint32_t>(parse_int(value(), "seed"));
        else if (a == L"--net-players") o.players = std::clamp(parse_int(value(), "player count"), 2, 4);
        else if (a == L"--net-delay") o.delay = std::clamp(parse_int(value(), "delay"), 2, 20);
        else if (a == L"--net-trace") o.trace = value();
        else if (a == L"--net-bot") o.bot = value();
        else if (a == L"--net-exit") o.exit_ms = parse_int(value(), "exit time");
        else if (a == L"--net-wait") o.wait_s = std::max(1, parse_int(value(), "wait time"));
        else if (a == L"--net-fast") o.fast = true;
        else if (a == L"--net-check-maps") o.check_maps = true;
        else if (a == L"--net-test-pointer") g_test_pointer = true;
        else if (a == L"--net-test-desync") o.test_desync_ms = parse_int(value(), "desync time");
        else if (a == L"--net-test-lost") std::tie(o.test_lost_ms, o.test_lost_length) = parse_pair(value(), "--net-test-lost");
        else if (a == L"--net-test-hang") std::tie(o.test_hang_ms, o.test_hang_length) = parse_pair(value(), "--net-test-hang");
        else if (a == L"--net-test-turret") std::tie(o.test_turret_ms, o.test_turret_length) = parse_pair(value(), "--net-test-turret");
        else if (a == L"--net-test-drive") std::tie(o.test_drive_ms, o.test_drive_length) = parse_pair(value(), "--net-test-drive");
        else if (a == L"--net-manual") std::tie(o.manual_control, o.manual_bonus) = parse_pair(value(), "--net-manual");
        else if (a == L"--net-rejoin") {
            o.role = Role::Client;
            o.rejoin = true;
            parse_endpoint(value(), o);
        }
        else if (a == L"--net-rejoin-key") o.rejoin_key = utils::from_wstring(value());
        else if (a == L"--net-test-crash") o.test_crash_ms = parse_int(value(), "crash time");
        else if (a == L"--net-test-remove") o.test_remove_ms = parse_int(value(), "remove time");
        else if (a == L"--net-dropped") {
            const std::wstring v(value());
            if (v != L"ai" && v != L"idle") throw std::runtime_error("--net-dropped takes ai or idle");
            o.dropped_ai = v == L"ai" ? 1 : 0;
        }
        else if (a == L"--net-test-dump") o.test_dump_ms = parse_int(value(), "dump time");
        else if (a == L"--net-mods") o.mods = Mods::split(utils::from_wstring(value()));
        else if (a == L"--lobby-host" || a == L"--lobby-join") {
            o.role = a == L"--lobby-host" ? Role::Host : Role::Client;
            o.lobby = true;
            parse_endpoint(value(), o);
        }
        else if (a == L"--lobby-local") {
            o.lobby = true;
            o.lobby_local = true;
        }
        else if (a == L"--lobby-map") o.lobby_map = value();
        else if (a == L"--lobby-start") o.lobby_start = std::clamp(parse_int(value(), "lobby start count"), 1, Lobby::max_people);
        else if (a == L"--lobby-ready") o.lobby_ready = true;
        else if (a == L"--lobby-return") o.lobby_return = true;
        else if (a == L"--lobby-test-size") o.lobby_test_size = parse_pair(value(), "--lobby-test-size");
        else if (a == L"--lobby-side") o.lobby_side = std::clamp(parse_int(value(), "lobby side"), 1, BattleRules::side_count);
        else if (a == L"--lobby-name") {
            o.lobby_name = value();
            if (!PlayerName::valid(o.lobby_name)) throw std::runtime_error("Not a valid player name on the command line");
        }
        else if (a == L"--lobby-bonus") {
            o.lobby_bonus = BattleRules::parse_bonus(utils::from_wstring(value()));
            if (!o.lobby_bonus) throw std::runtime_error("Unknown bonus on the command line");
        }
        else throw std::runtime_error("Unknown argument " + utils::from_wstring(a));
    }
    arguments = std::move(rest);
    return o;
}

std::string start_line(const Start &s) {
    std::string names;
    bool any = false;
    for (const std::wstring &name : s.names) {
        any = any || PlayerName::valid(name);
        names += (names.empty() ? "" : ",") + (PlayerName::valid(name) ? utils::from_wstring(name) : std::string("-"));
    }
    return std::format("start side={} host={} seed={} humans={} turn={} delay={} step={}{}{}{} map={}", s.side, s.host_side,
                       s.seed, side_list(s.human_sides), s.config.turn_ms, s.config.delay, s.config.step_ms,
                       any ? " names=" + names : std::string(), s.key.empty() ? std::string() : " key=" + s.key,
                       s.map_hash.empty() ? std::string() : " maphash=" + s.map_hash, utils::from_wstring(s.map));
}

std::optional<Start> parse_start(std::string_view line) {
    if (!line.starts_with("start ")) return std::nullopt;
    Start s;
    const auto map_at = line.find(" map=");
    if (map_at == std::string_view::npos) return std::nullopt;
    s.map = utils::to_wstring(line.substr(map_at + 5));
    std::istringstream in{std::string(line.substr(6, map_at - 6))};
    std::string pair;
    int seen = 0;
    while (in >> pair) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const std::string key = pair.substr(0, eq), value = pair.substr(eq + 1);
        if (key == "side") s.side = std::atoi(value.c_str());
        else if (key == "host") s.host_side = std::atoi(value.c_str());
        else if (key == "seed") s.seed = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "humans") s.human_sides = parse_sides(value);
        else if (key == "turn") s.config.turn_ms = std::atoi(value.c_str());
        else if (key == "delay") s.config.delay = std::atoi(value.c_str());
        else if (key == "step") s.config.step_ms = std::atoi(value.c_str());
        else if (key == "names") {  // optional
            std::string_view list = value;
            for (std::size_t side = 0; side < s.names.size() && !list.empty(); ++side) {
                const auto comma = list.find(',');
                const auto item = list.substr(0, comma);
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
                if (item == "-") continue;
                try {
                    s.names[side] = utils::to_wstring(item);
                }
                catch (...) {
                }
                if (!PlayerName::valid(s.names[side])) s.names[side].clear();
            }
            continue;
        }
        else if (key == "key") {  // optional
            s.key = value;
            continue;
        }
        else if (key == "maphash") {  // optional
            s.map_hash = value;
            continue;
        }
        else continue;
        ++seen;
    }
    if (seen != 7 || s.map.empty() || s.side < 1 || s.side > BattleRules::side_count || s.config.step_ms <= 0 ||
        s.config.turn_ms <= 0 || s.config.turn_ms % s.config.step_ms != 0 || s.config.delay < 1 ||
        !(s.human_sides & (1u << s.side)) || s.host_side < 1 || s.host_side > BattleRules::side_count ||
        !(s.human_sides & (1u << s.host_side)))
        return std::nullopt;
    return s;
}

namespace {

// Coming back failed: why, then the startup menu (the game starts again there).
void back_failed(const wchar_t *russian_text, const wchar_t *english, const std::string &log) {
    lgr.error("Net: cannot come back to the battle: {}")(log);
    forget_rejoin_ticket();
    const bool ru = system_russian();
    MessageBoxW(nullptr, ru ? russian_text : english,
                ru ? L"Возвращение в бой" : L"Back to the battle",
                MB_OK | MB_ICONWARNING);
    g_restart_arguments.clear();
    restart_to_menu();
}

// A person who lost the connection comes back: the host gives the battle's start again (the same map, seed,
// rules and side) once it knows the key; the rest comes in the battle (Lockstep's history).
std::optional<Start> connect_back(const Options &options, const StandaloneSettings::Values &settings) {
    std::string key = options.rejoin_key;
    if (key.empty())
        if (const auto ticket = rejoin_ticket()) key = ticket->key;
    if (key.empty()) key = "name";  // the host knows the person by their name (take_back)
    auto link = NetLink::connect(options.address, options.port, back_connect_ms);
    if (!link) {
        back_failed(L"Хост не отвечает: бой, вероятно, уже закончен.",
                    L"The host does not answer: the battle has probably ended.", "unreachable");
        return std::nullopt;
    }
    const std::wstring name = !options.lobby_name.empty() ? options.lobby_name : PlayerName::load();
    link->send_line(Lobby::hello_line(settings.battle_bonus, GameData::data_fingerprint(), GameData::build_fingerprint(), name, key));
    link->flush(5000);
    std::optional<BattleRules::Rules> rules;
    std::optional<Start> start;
    std::string line;
    while (!start && link->wait_line(line, back_connect_ms)) {
        if (line.starts_with("rules ")) rules = BattleRules::from_text(std::string_view(line).substr(6));
        else if (line.starts_with("start ")) start = parse_start(line);
        else if (line.starts_with("refused")) break;
    }
    if (!start || !rules) {
        const bool r = line.starts_with("refused rejoin ");
        if (r && line.ends_with("unknown"))
            back_failed(L"Хост не нашёл в бою вылетевшего игрока с вашим ником.",
                        L"The host has no player gone from the battle with your name.", line);
        else if (r && line.ends_with("over"))
            back_failed(L"Бой уже закончился.", L"The battle is over.", line);
        else if (r && line.ends_with("beaten"))
            back_failed(L"Ваша сторона уже разбита.", L"Your side is already beaten.", line);
        else if (r && line.ends_with("left"))
            back_failed(L"Вы вышли из этого боя или хост выгнал вас: вернуться нельзя.",
                        L"You left this battle or the host removed you: you cannot come back.", line);
        else if (line.starts_with("refused build") || line.starts_with("refused data") || line.starts_with("refused version"))
            back_failed(L"У хоста другая сборка игры или другие файлы.",
                        L"The host has another build or other game files.", line);
        else
            back_failed(L"Этот бой уже закончился или хост играет другой.",
                        L"This battle has ended or the host plays another one.", line.empty() ? "no answer" : line);
        return std::nullopt;
    }
    start->rules = *rules;
    // A map the host gave for this battle: its copy, kept while the person may come back.
    if (!start->map_hash.empty() && !MapShare::used(start->map)) {
        const auto own = MapShare::read(start->map);
        if ((!own || MapShare::fingerprint(*own) != start->map_hash) && !MapShare::use(start->map, start->map_hash)) {
            back_failed(L"На этом ПК нет карты этого боя.", L"This PC does not have the battle's map.", "no map");
            return std::nullopt;
        }
    }
    g_links.push_back(std::move(link));
    lgr.warning("Net: coming back to the battle as side {}")(start->side);
    return start;
}

}  // namespace

std::optional<RejoinTicket> rejoin_ticket() {
    std::ifstream in{std::filesystem::path(ticket_file)};
    if (!in) return std::nullopt;
    RejoinTicket t;
    long long time = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "host") t.address = value;
        else if (key == "port") t.port = std::atoi(value.c_str());
        else if (key == "key") t.key = value;
        else if (key == "side") t.side = std::atoi(value.c_str());
        else if (key == "time") time = std::atoll(value.c_str());
        else if (key == "map") {
            try {
                t.map = utils::to_wstring(value);
            }
            catch (...) {
            }
        }
    }
    const long long age = static_cast<long long>(std::time(nullptr)) - time;
    if (t.address.empty() || t.port <= 0 || t.key.empty() || age < 0 || age > ticket_lifetime_s) return std::nullopt;
    return t;
}

void forget_rejoin_ticket() {
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(ticket_file), error);
}

void request_rejoin(const std::wstring &name) {
    const auto ticket = rejoin_ticket();
    if (!ticket) return;
    g_return_to_menu = true;
    g_restart_arguments = rejoin_arguments(ticket->address, ticket->port, name);
}

void request_rejoin_at(const std::string &address, int port, const std::wstring &name) {
    // A ticket for another battle would give the host a key it does not know: it then looks for the name.
    if (const auto ticket = rejoin_ticket(); ticket && (ticket->address != address || ticket->port != port))
        forget_rejoin_ticket();
    g_return_to_menu = true;
    g_restart_arguments = rejoin_arguments(address, port, name);
}

std::optional<Start> connect(const Options &options, const StandaloneSettings::Values &settings) {
    g_links.clear();
    if (!NetLink::startup()) {
        fail("Windows sockets are unavailable");
        return std::nullopt;
    }
    if (options.rejoin) return connect_back(options, settings);
    const int wait_ms = options.wait_s * 1000;
    if (options.role == Role::Host) {
        NetLink::Listener listener(options.address, options.port);
        if (!listener.listening()) {
            fail(std::format("Cannot listen on {}:{} (is another host running?)", options.address, options.port));
            return std::nullopt;
        }
        lgr.warning("Net: hosting on {}:{}, waiting for {} more")(options.address, options.port, options.players - 1);
        Start start;
        start.map = host_map(options, settings);
        start.seed = options.seed.value_or(static_cast<std::uint32_t>(std::time(nullptr)));
        start.config.delay = options.delay > 0 ? options.delay : 2;
        start.rules = StandaloneSettings::battle_rules(settings);
        start.rules.manual_control = options.manual_control != 0;
        start.rules.manual_bonus = options.manual_bonus != 0;
        start.rules.dropped_ai = options.dropped_ai != 0;
        if (options.mods) start.rules.mods = *options.mods;
        start.rules.bonus.fill(BattleChoice::Bonus::None);
        start.rules.bonus[0] = settings.battle_bonus;  // the host plays side 1
        start.side = 1;
        start.host_side = 1;
        start.human_sides = 1u << 1;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        while (int(g_links.size()) < options.players - 1) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
            auto link = left.count() > 0 ? listener.accept(int(left.count())) : nullptr;
            if (!link) {
                g_links.clear();
                fail("Nobody joined the battle in time");
                return std::nullopt;
            }
            std::string line;
            const auto hello = link->wait_line(line, 10000) ? Lobby::parse_hello(line) : std::nullopt;
            if (!hello) {
                lgr.warning("Net: a connection did not introduce itself: {}")(line);
                continue;
            }
            if (hello->version != Lobby::protocol_version) {
                link->send_line(std::format("refused version {}", Lobby::protocol_version));
                link->flush(1000);
                lgr.warning("Net: refused a client with protocol {}")(hello->version);
                continue;
            }
            if (hello->build != GameData::build_fingerprint() || hello->data != GameData::data_fingerprint()) {
                link->send_line(hello->build != GameData::build_fingerprint() ? "refused build" : "refused data");
                link->flush(1000);
                lgr.warning("Net: refused a client with other files (build {}, data {})")(hello->build, hello->data);
                continue;
            }
            const int side = int(g_links.size()) + 2;
            start.rules.bonus[side - 1] = hello->bonus;
            start.human_sides |= 1u << side;
            if (PlayerName::valid(hello->name)) start.names[std::size_t(side - 1)] = hello->name;  // (a person coming back by name)
            g_links.push_back(std::move(link));
            lgr.warning("Net: side {} joined (bonus {})")(side, BattleRules::bonus_name(hello->bonus));
        }
        return start;  // sent to the clients once the host has loaded (run)
    }

    auto link = NetLink::connect(options.address, options.port, wait_ms);
    if (!link) {
        fail(std::format("Cannot reach the host at {}:{}", options.address, options.port));
        return std::nullopt;
    }
    link->send_line(Lobby::hello_line(settings.battle_bonus, GameData::data_fingerprint(), GameData::build_fingerprint(),
                                      !options.lobby_name.empty() ? options.lobby_name : PlayerName::load()));
    link->flush(5000);
    std::optional<BattleRules::Rules> rules;
    std::optional<Start> start;
    std::string line;
    while (!start && link->wait_line(line, wait_ms)) {
        if (line.starts_with("rules ")) rules = BattleRules::from_text(std::string_view(line).substr(6));
        else if (line.starts_with("start ")) start = parse_start(line);
        else if (line.starts_with("refused")) break;
    }
    if (!start || !rules) {
        fail("The host did not start the battle: " + line);
        return std::nullopt;
    }
    start->rules = *rules;
    g_links.push_back(std::move(link));
    return start;
}

Start lobby_start(const Lobby::State &state, const std::vector<int> &sides, const StandaloneSettings::Values &settings,
                  std::uint32_t seed, int delay) {
    Start start;
    start.map = state.map;
    start.map_hash = state.map_hash;
    start.seed = seed;
    start.config.delay = delay;
    start.rules = StandaloneSettings::battle_rules(settings);
    start.rules.ai_resources = state.ai_resources;
    start.rules.manual_control = state.manual_control;
    start.rules.manual_bonus = state.manual_bonus;
    start.rules.dropped_ai = state.dropped_ai;
    start.rules.mods.clear();
    for (const Lobby::ModRef &mod : state.mods) start.rules.mods.push_back(mod.id);
    start.rules.bonus.fill(BattleChoice::Bonus::None);
    for (std::size_t i = 0; i < sides.size() && i < state.people.size(); ++i) {
        start.rules.bonus[sides[i] - 1] = state.people[i].bonus;
        start.names[sides[i] - 1] = state.people[i].name;
        start.human_sides |= 1u << sides[i];
    }
    start.side = start.host_side = sides.empty() ? 1 : sides[0];
    return start;
}

void prepare(Options options, Start start, std::vector<std::unique_ptr<NetLink::Connection>> links) {
    g_links = std::move(links);
    g_prepared = Prepared{std::move(options), std::move(start)};
}

const std::optional<Prepared> &prepared() { return g_prepared; }

bool return_to_menu() { return g_return_to_menu; }

bool local_battle() { return g_prepared && g_prepared->options.role == Role::None; }

bool test_pointer() { return g_test_pointer; }

void after_local_battle(bool to_lobby) {
    g_return_to_menu = true;
    g_restart_arguments = to_lobby ? L" --lobby-local" : L"";
}

void cancel_return() {
    g_return_to_menu = false;
    g_restart_arguments.clear();
}

const std::wstring &restart_arguments() { return g_restart_arguments; }

bool restart_to_menu() {
    const auto executable = Startup::ExecutablePath();
    std::wstring command = L"\"" + executable.native() + L"\"" + g_restart_arguments;
    const std::wstring directory = executable.parent_path().native();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                        &startup, &process)) {
        lgr.error("Net: cannot start the game again at its menu (Windows error {})")(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    lgr.notice(g_restart_arguments.empty() ? "Net: the battle is over; the game starts again at its menu"
                                           : "Net: the battle is over; the game goes back to its lobby");
    return true;
}

int run(const Options &options, const Start &start) {
    std::vector<int> clients;
    for (int side = 1; side <= BattleRules::side_count; ++side)
        if (side != start.host_side && (start.human_sides & (1u << side))) clients.push_back(side);
    auto session = options.role == Role::Host ? Lockstep::Session::host(start.config, start.side, clients)
                   : options.rejoin           ? Lockstep::Session::rejoining(start.config, start.side, start.host_side)
                                              : Lockstep::Session::client(start.config, start.side, start.host_side);
    auto battle = std::make_unique<Battle>(options, start, std::move(session));
    Battle &b = *battle;

    if (options.role == Role::Host) {
        // Each client's key: only that person may come back after losing the connection.
        std::random_device random_device;
        for (int side : clients)
            b.keys[std::size_t(side - 1)] = std::format("{:08x}{:08x}", random_device(), random_device());
        // The clients load only now: two processes loading a map for the first time at once would
        // both write its minimap picture to the game's cache folder.
        for (std::size_t i = 0; i < g_links.size(); ++i) {
            Start theirs = start;
            theirs.side = clients[i];
            theirs.key = b.keys[std::size_t(clients[i] - 1)];
            g_links[i]->send_line("rules " + BattleRules::to_text(start.rules));
            g_links[i]->send_line(start_line(theirs));
            g_links[i]->flush(5000);
        }
        b.listener = std::make_unique<NetLink::Listener>(options.address, options.port);
        if (!b.listener->listening()) {
            note(b, std::format("cannot listen on {}:{}: nobody can come back", options.address, options.port));
            b.listener.reset();
        }
    }
    else {
        save_ticket(options, start);
    }
    if (options.rejoin) {
        // Replays the battle up to now before it is shown: undrawn, silent, without this person's orders.
        b.catching = true;
        CMatrixMap::m_CatchingUp = true;
        Commands::set_interface_input(false);
        SoundBridge::master_volume(0.0f);
        b.muted = true;
    }

    // A shared battle: people act only through commands; nobody's menu or dialog stops it.
    Commands::set_deferred(true);
    if (g_MatrixMap->m_HumanSides != start.human_sides) g_MatrixMap->SetHumanSides(start.human_sides);
    g_MatrixMap->LeaveDialogMode();
    g_MatrixMap->Pause(false);
    g_MatrixMap->m_PauseForbidden = true;

    if (!options.trace.empty()) {
        b.trace.open(std::filesystem::path(options.trace), std::ios::trunc);
        b.trace << std::format("net-trace 1 role={} side={} map={} seed={} humans={} turn={} delay={} step={}\n",
                               options.role == Role::Host ? "host" : "client", start.side,
                               utils::from_wstring(g_MatrixMap->MapName()), start.seed, side_list(start.human_sides),
                               start.config.turn_ms, start.config.delay, start.config.step_ms);
        b.trace << "rules " << BattleRules::to_text(BattleRules::active()) << '\n';
    }
    // Every battle's commands, in the lab's replay format: a desync can then be replayed here with other
    // camera views (the last battle only; written next to the EXE).
    b.commands.open(std::filesystem::path(L"battle-commands.txt"), std::ios::trunc);
    b.commands << std::format("simlab-commands 1 map={} seed={} step_ms={} human={}\n",
                              utils::from_wstring(g_MatrixMap->MapName()), start.seed, start.config.step_ms,
                              side_list(start.human_sides));
    b.commands << "rules " << BattleRules::to_text(BattleRules::active()) << '\n';
    if (!options.bot.empty()) b.bot = load_bot(options.bot, start.side);

    Commands::set_router([&b](Commands::Command c) { b.session.issue(std::move(c), g_MatrixMap->GetTime()); });
    Commands::set_observer([&b](const Commands::Command &c) {
        if (b.trace.is_open()) b.trace << "cmd " << Commands::to_line(c) << '\n';
        if (b.commands.is_open()) b.commands << Commands::to_line(c) << '\n';
        if (c.type == Commands::Type::Surrender) BattleStats::set_gone(c.side, c.params.empty() ? 0 : c.params[0]);
        if (c.type == Commands::Type::Rejoin) {
            BattleStats::set_back(c.side);
            b.gone_sides &= ~(1u << c.side);
            const bool ru = russian();
            if (c.side == b.start.side) {
                if (b.catching) return;  // a person who came back before, in the replayed past
                Commands::set_interface_input(true);
                note(b, "this person commands the side again");
                BattleNotice::show(ru ? L"Вы вернулись в бой" : L"You are back in the battle");
            }
            else if (!b.catching) {
                BattleNotice::show(person_of(b, c.side, ru) + (ru ? L" вернулся в бой" : L" is back in the battle"));
            }
            return;
        }
        if (c.type == Commands::Type::Surrender && c.side != b.start.side && !b.catching) {
            // Every peer runs it, so every window says so at the top right of the screen.
            // 0 gave up, 1 left, 2 lost the connection, 3 removed by the host
            const int how = c.params.empty() ? 0 : c.params[0];
            if (how >= 1 && (b.gone_sides & (1u << c.side))) return;  // once per person
            if (how >= 1) b.gone_sides |= 1u << c.side;
            const bool ru = russian();
            const wchar_t *what = how == 3 ? (ru ? L"\u043e\u0442\u043a\u043b\u044e\u0447\u0451\u043d \u0445\u043e\u0441\u0442\u043e\u043c" : L"was removed by the host")
                                  : how == 2 ? (ru ? L"\u043f\u043e\u0442\u0435\u0440\u044f\u043b \u0441\u0432\u044f\u0437\u044c" : L"lost the connection")
                                  : how == 1 ? (ru ? L"\u0432\u044b\u0448\u0435\u043b \u0438\u0437 \u0431\u043e\u044f" : L"left the battle")
                                             : (ru ? L"\u0441\u0434\u0430\u043b\u0441\u044f" : L"gave up");
            const std::wstring &name = b.start.names[std::size_t(c.side - 1) % b.start.names.size()];
            // "Player Rover (Keller) left the battle"; without a name, the faction alone.
            const std::wstring who = name.empty() ? std::wstring(Factions::name(c.side, ru))
                                                  : (ru ? L"\u0418\u0433\u0440\u043e\u043a " : L"Player ") + name + L" (" + Factions::name(c.side, ru) + L")";
            BattleNotice::show(who + L" " + what);
        }
    });

    BattleStats::set_names(start.names);
    b.session.ready();
    b.clock = std::chrono::steady_clock::now();
    update_title(b);
    preload_turrets(b);
    note(b, std::format("battle loaded; side {} of {}; fpcw={:#010x}", start.side, side_list(start.human_sides), _controlfp(0, 0)));

    Loop::Platform platform{&b, loop_now, loop_poll, loop_dispatch, loop_wait};
    platform.advance = loop_advance;
    platform.step_ms = start.config.step_ms;
    const int result = L3GRun(platform);

    note(b, "leaving");
    if (b.commands.is_open()) b.commands << std::format("end {}\n", g_MatrixMap->GetTime());
    b.commands.close();
    CMatrixMap::m_TestDeviceLost = false;
    CMatrixMap::m_DeviceLost = false;
    if (b.catching) finish_catching(b, g_MatrixMap->GetTime());
    BattleWait::clear();
    BattleNotice::clear();
    BattleChat::clear();
    BattleStats::clear();
    BattleStats::clear_names();
    // A client's window closed during the battle (the cross, Alt+F4): not leaving it, so no goodbye; the host
    // sees the connection fail and the person can come back after starting the game again.
    const bool closed = options.role == Role::Client && FLAG(g_Flags, GFLAG_APPCLOSE) && !b.decided &&
                        b.session.dropped() == 0 && b.session.peer_live(0) && !b.session.alone();
    if (closed) note(b, "the window was closed: the person may come back");
    for (auto &link : g_links) {
        if (!link || b.rejoin_restart || closed) continue;  // (a goodbye would be leaving for good)
        link->send_line("bye");
        link->flush(1000);
    }
    g_links.clear();
    b.coming_back.clear();
    b.listener.reset();
    // The way back stays while this person may still want it: the connection failed, not a goodbye.
    const bool may_come_back = b.rejoin_restart || closed || b.session.dropped() != 0 ||
                               (options.role == Role::Client && !b.session.peer_live(0) && b.session.peer_how_gone(0) == 2 &&
                                b.session.dropped() == 0 && !b.decided);
    if (options.role == Role::Client && !may_come_back) forget_rejoin_ticket();
    if (b.trace.is_open()) {
        b.trace << std::format("end t={} desync={}\n", g_MatrixMap->GetTime(), b.session.first_desync());
        b.trace.close();
    }
    Commands::reset();
    // A person who started from the menu goes back there (tests and command-line battles just end):
    // to the lobby after staying to the battle's end, else to the menu. A host who closed the final
    // statistics first has gone to its lobby already; one who went before the end has not.
    // (A battle come back to is one from the menu too; tests end theirs with --net-exit.)
    g_return_to_menu = (g_prepared || (options.rejoin && options.exit_ms < 0)) && (!options.lobby || options.lobby_return) &&
                       !FLAG(g_Flags, GFLAG_APPCLOSE);
    g_restart_arguments.clear();
    if (b.rejoin_restart) {
        g_return_to_menu = true;
        g_restart_arguments = rejoin_arguments(options.address, options.port, options.lobby_name);
        note(b, "starting again to come back");
        return result;
    }
    if (g_return_to_menu && b.decided && g_ExitState != 1 && !b.host_left_early) {
        const std::string endpoint = std::format("{}:{}", options.address, options.port);
        g_restart_arguments = options.role == Role::Host
                                  ? L" --lobby-host " + utils::to_wstring(endpoint) + L" --lobby-map \"" +
                                        std::filesystem::path(start.map).stem().wstring() + L"\""
                                  : L" --lobby-join " + utils::to_wstring(endpoint);
        if (!options.lobby_name.empty()) g_restart_arguments += L" --lobby-name " + options.lobby_name;
        g_restart_arguments += L" --lobby-return";
    }
    note(b, g_restart_arguments.empty() ? "back to the menu" : "back to the lobby");
    return result;
}

}  // namespace NetBattle
