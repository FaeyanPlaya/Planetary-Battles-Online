#include <algorithm>
// MatrixGame - licensed under GPLv2 or any later version.
#include "SimLab.hpp"

#include "CBlockPar.hpp"
#include "LoopPlatform.hpp"
#include "MatrixMap.hpp"
#include "MatrixLogic.hpp"
#include "MatrixSide.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixFlyer.hpp"
#include "random.hpp"
#include "utils.hpp"
#include "3g.hpp"
#include "Commands.hpp"

#include <float.h>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <stupid_logger.hpp>

namespace SimLab {

namespace {

struct Fnv {
    std::uint64_t value = 14695981039346656037ULL;
    void bytes(const void *data, std::size_t size) {
        const auto *p = static_cast<const unsigned char *>(data);
        for (std::size_t i = 0; i < size; ++i) {
            value ^= p[i];
            value *= 1099511628211ULL;
        }
    }
    void i(std::int64_t v) { bytes(&v, sizeof(v)); }
    void f(float v) { std::uint32_t b; std::memcpy(&b, &v, sizeof(b)); i(b); }
};

std::uint32_t bits(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

// One keyboard or mouse window message, keyed by the loop iteration that dispatched it.
struct InputEvent {
    int time;  // game time (ms) at dispatch
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
};

struct Run {
    Options options;
    std::ofstream trace;
    std::ofstream record;
    std::ofstream commands;
    std::vector<InputEvent> replay;
    std::size_t replay_pos = 0;
    int replay_end_time = -1;
    bool injecting = false;
    Loop::Clock::time_point clock{};
    int frames = 0;
    bool initial_dumped = false;  // the start state is always dumped, to diagnose launch-to-launch drift
    int next_hash_ms = 0;
    int next_camera_ms = 0;
    int camera_index = 0;
} *g_run = nullptr;

bool is_input(UINT m) {
    return (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_INPUT;
}

bool load_replay(const std::wstring &path, Run &run) {
    std::ifstream in{std::filesystem::path(path)};
    if (!in) return false;
    std::string tag;
    while (in >> tag) {
        if (tag == "F") {
            InputEvent e{};
            unsigned long long w = 0, l = 0;
            in >> e.time >> std::hex >> e.message >> w >> l >> std::dec;
            e.wparam = static_cast<WPARAM>(w);
            e.lparam = static_cast<LPARAM>(static_cast<LONG_PTR>(l));
            run.replay.push_back(e);
        }
        else if (tag == "end") {
            in >> run.replay_end_time;
        }
        else {
            std::string rest;
            std::getline(in, rest);
        }
    }
    return true;
}

// The "rules ..." line of a command or input record.
std::optional<BattleRules::Rules> read_rules(const std::wstring &path) {
    std::ifstream in{std::filesystem::path(path)};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.starts_with("rules ")) continue;
        auto rules = BattleRules::from_text(std::string_view(line).substr(6));
        if (!rules) lgr.error("SimLab: bad rules line: {}")(line);
        return rules;
    }
    lgr.warning("SimLab: {} has no rules line; the battle uses this PC's settings")(utils::from_wstring(path));
    return std::nullopt;
}

bool load_commands(const std::wstring &path, Run &run, bool sets_end) {
    std::ifstream in{std::filesystem::path(path)};
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("C ")) {
            Commands::Command command;
            if (!Commands::from_line(line, command)) {
                lgr.error("SimLab: bad command line: {}")(line);
                return false;
            }
            Commands::schedule(command);
        }
        else if (sets_end && line.starts_with("end ")) {
            run.replay_end_time = std::atoi(line.c_str() + 4);
        }
    }
    return true;
}

std::string side_list(unsigned sides) {
    std::string out;
    for (int id = 1; id < 32; ++id)
        if (sides & (1u << id)) out += (out.empty() ? "" : ",") + std::to_string(id);
    return out.empty() ? "0" : out;
}

int game_time() { return g_MatrixMap->GetTime(); }

// Logs each simulation draw with its call stack as module offsets, so two runs
// can be aligned and the caller resolved with addr2line on the same binary.
void observe_draw(std::uint64_t index) {
    if (!g_run || game_time() > g_run->options.trace_draws_until_ms ||
        game_time() < g_run->options.trace_draws_from_ms)
        return;
    void *frames[12];
    const USHORT n = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::string line = std::format("draw {} t={} frame={}", index, game_time(), g_run->frames);
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        line += std::format(" {:x}", a >= base ? a - base : a);
    }
    g_run->trace << line << '\n';
}

void move_camera() {
    // Deterministic tour over a 3x3 grid of map points; only the viewer changes.
    const float w = g_MatrixMap->m_Size.x * GLOBAL_SCALE;
    const float h = g_MatrixMap->m_Size.y * GLOBAL_SCALE;
    const int cell = g_run->camera_index++ % 9;
    const D3DXVECTOR2 target(w * (0.2f + 0.3f * float(cell % 3)), h * (0.2f + 0.3f * float(cell / 3)));
    g_MatrixMap->m_Camera.SetXYStrategy(target);
}

void record(bool detail = false) {
    const int t = game_time();
    std::string dump;
    const bool want_dump = g_run->options.dump || !g_run->initial_dumped || detail;
    g_run->initial_dumped = true;
    const auto hash = world_hash(want_dump ? &dump : nullptr);
    g_run->trace << std::format("frame={} t={} draws={} effects={} fpcw={:#010x} hash={:016x} cosmetic={} foreign={}\n",
                                g_run->frames, t, random::draw_count(), g_MatrixMap->GetEffectCount(), _controlfp(0, 0),
                                hash, random::cosmetic_draw_count(), random::foreign_thread_draw_count());
    if (!dump.empty()) g_run->trace << dump;
    g_run->trace.flush();
}

Loop::Clock::time_point lab_now(void *) {
    // Called once per loop iteration, after the previous frame's Takt and Draw.
    if (g_run->frames > 0) {
        const int t = game_time();
        if (t >= g_run->next_hash_ms) {
            record();
            g_run->next_hash_ms += g_run->options.hash_every_ms;
        }
        else if (t >= g_run->options.detail_from_ms && t <= g_run->options.detail_until_ms) {
            record(true);
        }
        if (g_run->options.camera_cell >= 0 && g_run->camera_index == 0) {
            g_run->camera_index = g_run->options.camera_cell;
            move_camera();  // once; the index is never 0 again
        }
        else if (g_run->options.camera == CameraPolicy::Orbit && t >= g_run->next_camera_ms) {
            move_camera();
            g_run->next_camera_ms += 2000;
        }
        const bool replay_done = g_run->replay_end_time >= 0 && t >= g_run->replay_end_time;
        if (t >= g_run->options.duration_ms || replay_done) {
            if (t % g_run->options.hash_every_ms != 0) record();  // final state for comparison
            g_run->trace << std::format("complete frame={} t={}\n", g_run->frames, t);
            g_run->trace.flush();
            SETFLAG(g_Flags, GFLAG_EXITLOOP);
        }
    }
    ++g_run->frames;
    g_run->clock += std::chrono::milliseconds(g_run->options.step_ms);
    return g_run->clock;
}

bool lab_poll(void *, MSG &message) {
    // Keep simulating while another window has focus.
    SETFLAG(g_Flags, GFLAG_APPACTIVE);
    // A replay delivers the recorded input of this iteration before any live message.
    if (g_run->replay_pos < g_run->replay.size() && g_run->replay[g_run->replay_pos].time <= game_time()) {
        const InputEvent &e = g_run->replay[g_run->replay_pos++];
        message = MSG{};
        message.hwnd = g_Wnd;
        message.message = e.message;
        message.wParam = e.wparam;
        message.lParam = e.lparam;
        g_run->injecting = true;
        return true;
    }
    return PeekMessage(&message, NULL, 0, 0, PM_REMOVE) != FALSE;
}

void lab_dispatch(void *, const MSG &message) {
    // With commands the interface only issues them: whatever it draws from the random
    // generator while handling input must not touch the battle's stream.
    random::CosmeticScopeIf interface_only(Commands::deferred());
    if (g_run->injecting) {
        g_run->injecting = false;
        DispatchMessage(&message);
        return;
    }
    if (is_input(message.message)) {
        // AI-only runs and replays must not depend on the person at the PC.
        const bool local_human = (g_run->options.human_sides >> PLAYER_SIDE) & 1u;
        if (!local_human || !g_run->options.replay.empty() || !g_run->options.command_replay.empty())
            return;
        if (g_run->record.is_open()) {
            g_run->record << std::format("F {} {:x} {:x} {:x}\n", game_time(), message.message,
                                         static_cast<unsigned long long>(message.wParam),
                                         static_cast<unsigned long long>(message.lParam));
        }
    }
    DispatchMessage(&message);
}
void lab_wait(void *) {}

int int_par(const Base::CBlockPar &bp, const wchar_t *name, int fallback) {
    return bp.ParCount(name) ? bp.ParGet(name).GetInt() : fallback;
}

} // namespace

Options load(const wchar_t *path) {
    Options o;
    if (!std::filesystem::exists(path)) return o;
    Base::CBlockPar bp;
    bp.LoadFromTextFile(path);
    o.active = true;
    if (bp.ParCount(L"Map")) {
        o.map = utils::trim(bp.ParGet(L"Map"));
        if (!o.map.empty() && !std::filesystem::path(o.map).has_extension()) o.map += L".cmap";
    }
    o.seed = static_cast<std::uint32_t>(int_par(bp, L"Seed", 1));
    o.step_ms = std::max(1, int_par(bp, L"StepMs", 10));
    o.duration_ms = std::max(1, int_par(bp, L"DurationMs", 60000));
    o.hash_every_ms = std::max(1, int_par(bp, L"HashEveryMs", 1000));
    o.dump = int_par(bp, L"Dump", 0) != 0;
    if (bp.ParCount(L"Camera")) {
        auto value = utils::trim(bp.ParGet(L"Camera"));
        utils::to_lower(value);
        o.camera = value == L"orbit" ? CameraPolicy::Orbit : CameraPolicy::Fixed;
    }
    o.camera_cell = std::clamp(int_par(bp, L"CameraCell", -1), -1, 8);
    if (bp.ParCount(L"Trace")) o.trace = utils::trim(bp.ParGet(L"Trace"));
    o.trace_draws_until_ms = int_par(bp, L"TraceDrawsUntilMs", -1);
    o.trace_draws_from_ms = int_par(bp, L"TraceDrawsFromMs", 0);
    o.detail_from_ms = int_par(bp, L"DetailFromMs", -1);
    o.detail_until_ms = int_par(bp, L"DetailUntilMs", -1);
    if (const int side = int_par(bp, L"HumanSide", 0); side > 0) o.human_sides = 1u << side;
    if (bp.ParCount(L"HumanSides")) {
        o.human_sides = 0;
        auto list = bp.ParGet(L"HumanSides");
        for (int i = 0, n = list.GetCountPar(L","); i < n; ++i) {
            const int side = list.GetStrPar(i, L",").GetInt();
            if (side > 0 && side < 32) o.human_sides |= 1u << side;
        }
    }
    o.view_side = int_par(bp, L"ViewSide", 1);
    if (bp.ParCount(L"CommandRecord")) o.command_record = utils::trim(bp.ParGet(L"CommandRecord"));
    if (bp.ParCount(L"CommandReplay")) o.command_replay = utils::trim(bp.ParGet(L"CommandReplay"));
    if (bp.ParCount(L"CommandScript")) o.command_script = utils::trim(bp.ParGet(L"CommandScript"));
    o.no_pause = int_par(bp, L"NoPause", 1) != 0;
    o.maintenance_ready = int_par(bp, L"MaintenanceReady", 0) != 0;
    if (bp.ParCount(L"Record")) o.record = utils::trim(bp.ParGet(L"Record"));
    if (bp.ParCount(L"Replay")) o.replay = utils::trim(bp.ParGet(L"Replay"));
    if (const auto &source = !o.command_replay.empty() ? o.command_replay : o.replay; !source.empty())
        o.rules = read_rules(source);
    if (bp.ParCount(L"Bonus")) {
        o.bonus.emplace();
        if (!BattleRules::bonuses_from_text(utils::from_wstring(utils::trim(bp.ParGet(L"Bonus"))), *o.bonus)) {
            lgr.error("SimLab: bad Bonus; expected four of None, AirSupport, ReinforcedArmor");
            o.bonus.reset();
        }
    }
    if (bp.ParCount(L"Resources")) {
        auto value = utils::trim(bp.ParGet(L"Resources"));
        utils::to_lower(value);
        if (value == L"map") o.resources = BattleRules::Resources::Map;
        else if (value == L"equal") o.resources = BattleRules::Resources::Equal;
        else lgr.error("SimLab: bad Resources; expected equal or map");
    }
    if (bp.ParCount(L"AiResources")) {
        auto value = utils::trim(bp.ParGet(L"AiResources"));
        utils::to_lower(value);
        if (value == L"map") o.ai_resources = 0;
        else if (value.size() == 2 && value[0] == L'x' && value[1] >= L'1' && value[1] <= L'4') o.ai_resources = value[1] - L'0';
        else lgr.error("SimLab: bad AiResources; expected map or x1..x4");
    }
    return o;
}

void begin(const Options &options) {
    // Without a human side every side is AI; a human side is the standalone player side.
    if (options.human_sides == 0) {
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE | MMFLAG_FULLAUTO);
    }
    else {
        // People's sides act only through commands, executed at the start of a step.
        Commands::set_deferred(true);
        if (g_MatrixMap->m_HumanSides != options.human_sides)  // normally set before the map loaded
            g_MatrixMap->SetHumanSides(options.human_sides);
    }
    // Initialization opened the begin dialog, which pauses until clicked; close it so
    // every run starts at the same frame.
    g_MatrixMap->LeaveDialogMode();
    g_MatrixMap->Pause(false);
    g_MatrixMap->m_PauseForbidden = options.no_pause;
    if (options.maintenance_ready) g_MatrixMap->SetMaintenanceTime(0);
    g_ScriptedInput = true;
    Commands::set_interface_input(((options.human_sides >> PLAYER_SIDE) & 1u) && options.command_replay.empty());
    g_run = new Run;
    g_run->options = options;
    g_run->trace.open(std::filesystem::path(options.trace), std::ios::trunc);
    g_run->trace << std::format(
            "header map={} seed={} step_ms={} duration_ms={} camera={} rng={} screen={}x{} human={} view={} input={}\n",
            utils::from_wstring(g_MatrixMap->MapName()), options.seed, options.step_ms, options.duration_ms,
            options.camera == CameraPolicy::Orbit ? "orbit" : "fixed",
            random::mode() == random::Mode::LegacyCRT ? "LegacyCRT" : "ParkMiller", g_ScreenX, g_ScreenY,
            side_list(options.human_sides), PLAYER_SIDE,
            !options.command_replay.empty() ? "commands"
            : !options.replay.empty()       ? "replay"
            : !options.record.empty()       ? "record"
                                            : "live");
    g_run->trace << "rules " << BattleRules::to_text(BattleRules::active()) << '\n';
    if (!options.command_script.empty() && !load_commands(options.command_script, *g_run, false))
        lgr.error("SimLab: cannot read command script {}")(utils::from_wstring(options.command_script));
    if (!options.command_replay.empty()) {
        if (!load_commands(options.command_replay, *g_run, true))
            lgr.error("SimLab: cannot read command replay {}")(utils::from_wstring(options.command_replay));
        g_run->trace << std::format("command replay end_time={}\n", g_run->replay_end_time);
    }
    if (!options.command_record.empty()) {
        g_run->commands.open(std::filesystem::path(options.command_record), std::ios::trunc);
        g_run->commands << std::format("simlab-commands 1 map={} seed={} step_ms={} human={}\n",
                                       utils::from_wstring(g_MatrixMap->MapName()), options.seed, options.step_ms,
                                       side_list(options.human_sides));
        g_run->commands << "rules " << BattleRules::to_text(BattleRules::active()) << '\n';
    }
    Commands::set_observer([](const Commands::Command &command) {
        const std::string line = Commands::to_line(command);
        if (g_run->commands.is_open()) g_run->commands << line << '\n';
        note("cmd " + line);
    });
    if (!options.replay.empty()) {
        if (!load_replay(options.replay, *g_run))
            lgr.error("SimLab: cannot read replay {}")(utils::from_wstring(options.replay));
        g_run->trace << std::format("replay events={} end_time={}\n", g_run->replay.size(), g_run->replay_end_time);
    }
    else if (!options.record.empty()) {
        g_run->record.open(std::filesystem::path(options.record), std::ios::trunc);
        g_run->record << std::format("simlab-input 1 map={} seed={} step_ms={} screen={}x{}\n",
                                     utils::from_wstring(g_MatrixMap->MapName()), options.seed, options.step_ms,
                                     g_ScreenX, g_ScreenY);
        g_run->record << "rules " << BattleRules::to_text(BattleRules::active()) << '\n';
    }
    if (options.trace_draws_until_ms >= 0) random::set_draw_observer(observe_draw);
    lgr.info("SimLab active: map {} seed {} step {} ms")(utils::from_wstring(g_MatrixMap->MapName()), options.seed,
                                                       options.step_ms);
}

int run(const Options &options) {
    (void)options;
    const Loop::Platform platform{nullptr, lab_now, lab_poll, lab_dispatch, lab_wait};
    const int result = L3GRun(platform);
    if (g_run->commands.is_open()) {
        // A command replay stops at this time, so both runs end on the same state.
        g_run->commands << std::format("end {}\n", game_time());
        g_run->commands.close();
    }
    Commands::reset();
    if (g_run->record.is_open()) {
        // A replay stops at this iteration, so both runs end on the same state.
        g_run->record << std::format("end {} frame={}\n", game_time(), g_run->frames);
        g_run->record.close();
    }
    if (g_run->replay_end_time < 0 || game_time() < g_run->replay_end_time) {
        record();  // final state of a run closed by the player
        g_run->trace << std::format("closed frame={} t={}\n", g_run->frames, game_time());
    }
    random::set_draw_observer(nullptr);
    delete g_run;
    g_run = nullptr;
    return result;
}

bool active() { return g_run != nullptr; }

void note(const std::string &event) {
    if (!g_run) return;
    g_run->trace << std::format("event t={} draws={} {}\n", game_time(), random::draw_count(), event);
}

// Logic-list position and the sim id commands address the object by.
std::string labelled(int index, const CMatrixMapStatic *ms) { return std::format("{} id={}", index, ms->GetSimId()); }

std::uint64_t world_hash(std::string *dump) {
    Fnv h;
    h.i(game_time());
    h.i(static_cast<std::int64_t>(random::draw_count()));
    for (int i = 0; i < g_MatrixMap->m_SideCnt; ++i) {
        const CMatrixSideUnit &s = g_MatrixMap->m_Side[i];
        h.i(s.m_Id);
        h.i(s.GetStatus());
        for (int r = 0; r < MAX_RESOURCES; ++r) h.i(s.GetResourcesAmount(static_cast<ERes>(r)));
        if (dump)
            *dump += std::format("  side {} status={} res={},{},{},{}\n", s.m_Id, int(s.GetStatus()),
                                 s.GetResourcesAmount(ERes(0)), s.GetResourcesAmount(ERes(1)),
                                 s.GetResourcesAmount(ERes(2)), s.GetResourcesAmount(ERes(3)));
    }
    int index = 0;
    for (CMatrixMapStatic *ms = CMatrixMapStatic::GetFirstLogic(); ms; ms = ms->GetNextLogic(), ++index) {
        const int type = ms->GetObjectType();
        if (type == OBJECT_TYPE_MAPOBJECT) continue;
        h.i(type);
        h.i(ms->GetSide());
        if (ms->IsRobot()) {
            CMatrixRobotAI *r = ms->AsRobot();
            h.f(r->m_PosX);
            h.f(r->m_PosY);
            h.f(r->GetHitPoint());
            h.i(r->m_CurrState);
            if (dump)
                *dump += std::format("  {} robot side={} pos={:08x},{:08x} ({:.3f},{:.3f}) hp={:08x} state={}\n", labelled(index, ms),
                                     ms->GetSide(), bits(r->m_PosX), bits(r->m_PosY), r->m_PosX, r->m_PosY,
                                     bits(r->GetHitPoint()), int(r->m_CurrState));
            if (dump) {
                // Detail for divergence hunts; not part of the hash.
                int target = -1, k = 0;
                for (CMatrixMapStatic *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic(), ++k)
                    if (o == r->GetEnv()->m_Target) target = k;
                *dump += std::format("    fwd={:08x},{:08x} hull={:08x},{:08x} wdir={:08x},{:08x},{:08x} target={} orders={}:{}",
                                     bits(r->m_Forward.x), bits(r->m_Forward.y), bits(r->m_HullForward.x),
                                     bits(r->m_HullForward.y), bits(r->GetWeaponDir().x), bits(r->GetWeaponDir().y),
                                     bits(r->GetWeaponDir().z), target, r->GetOrdersInPool(),
                                     r->GetOrdersInPool() ? int(r->GetOrder(0)->GetOrderType()) : -1);
                for (int w = 0; w < r->GetWeaponsCnt(); ++w)
                    if (r->GetWeapon(w).IsEffectPresent())
                        *dump += std::format(" w{}={}/{}/{}/{}", w, int(r->GetWeapon(w).GetWeaponType()),
                                             int(r->GetWeapon(w).m_On), r->GetWeapon(w).m_Heat,
                                             int(r->GetWeapon(w).IsFire()));
                *dump += std::format(" geo={:08x},{:08x},{:08x}\n", bits(r->GetGeoCenter().x),
                                     bits(r->GetGeoCenter().y), bits(r->GetGeoCenter().z));
                // What it fights: the attack target, the enemies it knows and those it ignores now.
                auto index_of = [](const CMatrixMapStatic *p) {
                    int i = 0;
                    for (CMatrixMapStatic *o = CMatrixMapStatic::GetFirstLogic(); o; o = o->GetNextLogic(), ++i)
                        if (o == p) return i;
                    return -1;
                };
                CInfo *env = r->GetEnv();
                *dump += std::format("    attack={} fire={} known=", index_of(env->m_TargetAttack),
                                     index_of(env->m_Target));
                for (CEnemy *e = env->m_FirstEnemy; e; e = e->m_NextEnemy) *dump += std::format("{},", index_of(e->m_Enemy));
                *dump += " ignored=";
                for (int i = 0; i < env->m_IgnoreCnt; ++i)
                    if (env->m_Ignore[i] && env->IsIgnore(env->m_Ignore[i]))
                        *dump += std::format("{},", index_of(env->m_Ignore[i]));
                *dump += std::format(" group={} maxfire={:.0f}\n", r->GetGroupLogic(), r->GetMaxFireDist());
                // How fast it goes: its group's and the column's limits, the last velocity, its matrix's slope.
                *dump += std::format("    team={} speed={:08x} group_speed={:08x} col_speed={:08x} max={:08x} vel={:08x},{:08x} "
                                     "up={:08x},{:08x},{:08x}\n",
                                     r->GetTeam(), bits(r->m_Speed), bits(r->m_GroupSpeed), bits(r->m_ColSpeed),
                                     bits(r->GetMaxSpeed()), bits(r->m_Velocity.x), bits(r->m_Velocity.y),
                                     bits(r->GetMatrix()._21), bits(r->GetMatrix()._22),
                                     bits(r->GetMatrix()._23));
            }
        }
        else if (ms->IsBuilding()) {
            CMatrixBuilding *b = ms->AsBuilding();
            h.f(b->GetHitPoint());
            h.i(b->m_State);
            if (dump)
                *dump += std::format("  {} building kind={} side={} hp={:08x} state={}\n", labelled(index, ms),
                                     int(b->m_Kind), ms->GetSide(),
                                     bits(b->GetHitPoint()), int(b->m_State));
        }
        else if (ms->IsCannon()) {
            CMatrixCannon *c = ms->AsCannon();
            h.f(c->GetHitPoint());
            h.i(c->m_CurrState);
            if (dump) {
                *dump += std::format("  {} cannon side={} hp={:08x} state={}\n", labelled(index, ms), ms->GetSide(),
                                     bits(c->GetHitPoint()), int(c->m_CurrState));
                // Detail for divergence hunts; not part of the hash.
                *dump += std::format("    angle={:08x} x={:08x} disp={:08x},{:08x},{:08x} target={}\n",
                                     bits(c->m_UnitCnt > 1 ? c->m_Unit[1].u1.s1.m_Angle : 0.0f), bits(c->m_AngleX),
                                     bits(c->m_TargetDisp.x), bits(c->m_TargetDisp.y), bits(c->m_TargetDisp.z),
                                     c->m_TargetCore && c->m_TargetCore->m_Object ? c->m_TargetCore->m_Object->GetSimId() : 0);
            }
        }
        else if (ms->IsFlyer()) {
            CMatrixFlyer *f = ms->AsFlyer();
            h.f(f->GetPos().x);
            h.f(f->GetPos().y);
            h.f(f->GetHitPoint());
            if (dump)
                *dump += std::format("  {} flyer side={} pos={:08x},{:08x} hp={:08x}\n", labelled(index, ms), ms->GetSide(),
                                     bits(f->GetPos().x), bits(f->GetPos().y), bits(f->GetHitPoint()));
        }
    }
    return h.value;
}

} // namespace SimLab
