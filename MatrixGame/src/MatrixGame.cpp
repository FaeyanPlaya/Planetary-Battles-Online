// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

// MatrixGame.cpp : Defines the entry point for the application.

#include "CStorage.hpp"
#include "CFile.hpp"

#include "MatrixGame.h"
#include "SoundBridge.hpp"
#include "StandaloneRuntime.hpp"
#include "MapSelection.hpp"
#include "MapCatalog.hpp"
#include "SettingsWindow.hpp"
#include "SettingsModel.hpp"
#include "MapWindow.hpp"
#include "StartupMenu.hpp"
#include "BattleLoading.hpp"
#include "BattleLaunch.hpp"
#include "ScreenLayout.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixMap.hpp"
#include "Interface/CInterface.h"
#include "MatrixRenderPipeline.hpp"
#include "ShadowStencil.hpp"
#include "MatrixLoadProgress.hpp"
#include "MatrixSkinManager.hpp"
#include "Interface/CIFaceMenu.h"
#include "Interface/MatrixHint.hpp"
#include "Interface/CHistory.h"
#include "MatrixSampleStateManager.hpp"
#include "MatrixMultiSelection.hpp"
#include "ExecutablePath.hpp"
#include "SessionCleanup.hpp"
#include "RandomConfiguration.hpp"
#include "BattleRules.hpp"
#include "ResourceFiles.hpp"
#include "SimLab.hpp"
#include "NetBattle.hpp"
#include "GameData.hpp"
#include "BattleStats.hpp"
#include "MapMetadata.hpp"
#include "SettingsModel.hpp"
#include <cwctype>
#include "MatrixGamePathUtils.hpp"
#include "StringConstants.hpp"
#include "WindowDisplay.hpp"
#include "Text/Font.hpp"
#include "input.hpp"

#include <new>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include "Commands.hpp"
#include <vector>

////////////////////////////////////////////////////////////////////////////////
#include <stupid_logger.hpp>
logger_type lgr{"test.log"};
////////////////////////////////////////////////////////////////////////////////

CHeap *g_MatrixHeap;
CBlockPar *g_MatrixData;
CMatrixMapLogic *g_MatrixMap;
CRenderPipeline *g_Render;
CLoadProgress *g_LoadProgress;

// The game configuration's own menu text is Cyrillic.
static bool standalone_russian(Base::CBlockPar *repl) {
    return SettingsModel::detect_language(repl->ParGetNE(L"setup")) == SettingsModel::Language::Russian;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    lgr.info("===== Started as EXE =====");

    int exit_code = 1;
    CFormMatrixGame *formgame = nullptr;
    bool initialization_started = false;
    bool battle_initialized = false;
    bool timer_active = false;
    std::string diagnostic;
    try {
        int numarg = 0;
        std::unique_ptr<LPWSTR, decltype(&LocalFree)> args(CommandLineToArgvW(GetCommandLineW(), &numarg), &LocalFree);
        if (!args || numarg < 1) {
            throw std::runtime_error(std::format("CommandLineToArgvW failed (Win32 error={})", GetLastError()));
        }
        const auto app_path = Startup::ExecutablePath();
        lgr.info(utils::from_wstring(app_path.native()));
        std::filesystem::current_path(app_path.parent_path());
        // Minimaps (and the map previews made from them) stay beside the EXE: Space Rangers HD clears
        // its own Documents\SpaceRangersHD\Cache when it starts.
        SetOutputFolder(FOLDER_NAME_CACHE, (app_path.parent_path() / L"Cache").wstring());
        std::vector<std::wstring_view> arguments;
        for (int i = 1; i < numarg; ++i) arguments.emplace_back(args.get()[i]);
        // --setup-yes (tests): a first start takes the first Space Rangers HD install found without asking.
        const bool setup_yes = std::erase(arguments, std::wstring_view(L"--setup-yes")) > 0;
        const auto net =NetBattle::parse(arguments);  // a battle shared over the network, if any
        const auto launch = BattleLaunch::classify(arguments);
        if (launch.selected && arguments.size() == 2) BattleLoading::connect(launch.event);
        StartupMenu::set_direct_start(launch.selected);
        if (net.role == NetBattle::Role::None || net.lobby) StartupMenu::set_network(net);
        if (net.role != NetBattle::Role::None) Display::set_exclusive_allowed(false);
        const wchar *map = !arguments.empty() && !launch.selected ? arguments[0].data() : nullptr;
        const bool calc_vis = map != nullptr;
        const auto lab = net.role == NetBattle::Role::None ? SimLab::load() : SimLab::Options{};
        if (lab.active && !lab.map.empty()) map = lab.map.c_str();
        // A replayed record brings the rules it was played with; a lab may set resources and bonuses.
        if (lab.active && lab.rules) {
            BattleRules::prescribe(lab.rules);
        }
        else if (lab.active && (lab.bonus || lab.resources || lab.ai_resources)) {
            auto rules = StandaloneSettings::battle_rules(StandaloneSettings::load().values);
            if (lab.bonus) rules.bonus = *lab.bonus;
            if (lab.resources) rules.resources = *lab.resources;
            if (lab.ai_resources) rules.ai_resources = *lab.ai_resources;
            BattleRules::prescribe(rules);
        }

        // A first start copies the files from the person's own Space Rangers HD; so does a start without the robots'
        // configuration (robots.dat, or the text files a developer may keep instead). Without it after all, the engine
        // reports the missing configuration itself.
        const bool configured = std::filesystem::exists(L"CFG\\robots.dat") || std::filesystem::exists(L"CFG\\robots\\data.txt");
        if ((!Startup::ResourceFileExists(L"DATA\\robots.pkg") || !configured) &&
            !GameData::setup(app_path.parent_path(), setup_yes) && !Startup::ResourceFileExists(L"DATA\\robots.pkg")) {
            lgr.error("Missing DATA/robots.pkg");
            BattleLoading::failed();
            MessageBoxW(nullptr, L"Place robots.pkg at DATA/robots.pkg.\nThe DATA folder is beside the game's EXE.",
                        L"Game resources required", MB_OK | MB_ICONINFORMATION);
            return exit_code;
        }

        // The host chooses the map, seed and rules of a shared battle; the others receive them.
        std::optional<NetBattle::Start> shared;
        if (net.role != NetBattle::Role::None && !net.lobby) {
            shared = NetBattle::connect(net, StandaloneSettings::load().values);
            if (!shared) return exit_code;
            BattleRules::prescribe(shared->rules);
            map = shared->map.c_str();
            g_PlayerSide = shared->side;
            g_InitialHumanSides = shared->human_sides;
        }

        uint32_t seed = shared ? shared->seed : lab.active ? lab.seed : (unsigned)time(nullptr);
        // The side this client shows and controls is fixed before the battle loads.
        if (lab.active) {
            g_PlayerSide = lab.view_side;
            // AI-only labs keep the classic rule whatever side is shown: side 1 is the
            // player's side, played by the AI in automatic mode.
            g_InitialHumanSides = lab.human_sides ? lab.human_sides : 1u << 1;
        }
        initialization_started = true;
        CGame::Init(hInstance, nullptr, map, seed);
        if (g_MatrixMap) {
            battle_initialized = true;
            MapSelection::finish_map_load();

            formgame = HNew(NULL) CFormMatrixGame();
            FormChange(formgame);
            if (BattleLoading::has_parent()) {
                const auto &display = StandaloneSettings::current().display;
                Display::request(display.mode, display.fullscreen);
            }
            ShowWindow(g_Wnd, SW_SHOWNORMAL);
            UpdateWindow(g_Wnd);

            timer_active = timeBeginPeriod(1) == TIMERR_NOERROR;

            if (shared)
            {
                NetBattle::run(net, *shared);
            }
            else if (const auto &lobby = NetBattle::prepared())
            {
                NetBattle::run(lobby->options, lobby->start);
            }
            else if (lab.active)
            {
                SimLab::begin(lab);
                SimLab::run(lab);
            }
            else if (calc_vis)
            {
                {
                    std::ofstream out("calcvis.log", std::ios::app);
                    std::string name = utils::from_wstring(g_MatrixMap->MapName());
                    out << name << " ...";
                }

                g_MatrixMap->CalcVis();

                {
                    std::ofstream out("calcvis.log", std::ios::app);
                    out << "done\n";
                }
            }
            else
            {
                L3GRun();
            }
        }

        exit_code = 0;
    }
    catch (const CException& ex)
    {
        ClipCursor(NULL);
        SoundBridge::shutdown();
#ifdef ENABLE_HISTORY
        CDebugTracer::SaveHistory();
#endif
        diagnostic = utils::from_wstring(ex.Info());
        lgr.fatal(diagnostic);
    }
    catch (const std::exception& e)
    {
        ClipCursor(nullptr);
        SoundBridge::shutdown();
        diagnostic = e.what();
        lgr.fatal(diagnostic);
    }
    catch (...) {
        ClipCursor(nullptr);
        SoundBridge::shutdown();
#ifdef ENABLE_HISTORY
        CDebugTracer::SaveHistory();
#endif
        diagnostic = "Unknown exception";
        lgr.fatal(diagnostic);
    }

    // Settings are read with the base library, so a failed map is forgotten before cleanup.
    if (!diagnostic.empty() && !battle_initialized && MapSelection::forget_remembered_map()) {
        diagnostic += "\n\nThe map chosen in the map selector could not be started and was forgotten. "
                      "The next start uses the configured map.";
    }
    if (initialization_started && !Session::cleanup_standalone(formgame, timer_active)) {
        lgr.fatal("Standalone session cleanup failed");
        exit_code = 1;
    }
    ClipCursor(nullptr);
    if (!diagnostic.empty()) {
        BattleLoading::failed();
        MessageBox(NULL, diagnostic.c_str(), "Exception:", MB_OK);
    }
    else if (exit_code == 0 && NetBattle::return_to_menu()) {
        NetBattle::restart_to_menu();
    }
    else if (exit_code == 0 && MapSelection::relaunch_requested() && !MapSelection::relaunch()) {
        MessageBoxW(nullptr, L"The game could not be started again on the chosen map. Start it again manually.",
                    L"Map", MB_OK | MB_ICONWARNING);
    }

    BattleLoading::failed();
    return exit_code;
}

bool Session::cleanup_standalone(CFormMatrixGame *&form, bool &timer_active) noexcept {
    return cleanup(
        [&] { if (timer_active) { timeEndPeriod(1); timer_active = false; } },
        [] { FormChange(nullptr); },
        [&] { if (form) { HDelete(CFormMatrixGame, form, nullptr); form = nullptr; } },
        [] { CGame::Deinit(); },
        [] { if (g_Cache) g_Cache->Clear(); },
        [] { L3GDeinit(); },
        [] { CacheDeinit(); },
        [] { CMain::BaseDeInit(); },
        [] { ClipCursor(nullptr); });
}

/**
 * Goes through and initializes all static variables.
 */
static void static_init(void) {
    // Base
    CMain::BaseInit();

    // 3G
#ifdef _DEBUG
    D3DResource::StaticInit();
#endif
#if (defined _DEBUG) && !(defined _RELDEBUG)
    CHelper::StaticInit();
#endif
    CVectorObject::StaticInit();
    CVOShadowProj::StaticInit();
    CVOShadowStencil::StaticInit();
    CForm::StaticInit();

    // Game
    CMatrixProgressBar::StaticInit();
    CMatrixMapStatic::StaticInit();
    Commands::reset();
    CMatrixMapObject::StaticInit();
    CMatrixFlyer::StaticInit();
    CMatrixMapGroup::StaticInit();
    CInterface::StaticInit();
    CMultiSelection::StaticInit();
    CTerSurface::StaticInit();
    CBottomTextureUnion::StaticInit();
    CSkinManager::StaticInit();
    CMatrixHint::StaticInit();
    CInstDraw::StaticInit();
    SInshorewave::StaticInit();

    g_Flags = 0;  // GFLAG_FORMACCESS;
}

void CGame::Init(HINSTANCE inst, [[maybe_unused]] HWND wnd, const wchar *map,uint32_t seed, const SRobotsSettings *provided_settings,
                const wchar *lang, const wchar *txt_start, const wchar *txt_win, const wchar *txt_loss,
                const wchar *planet)
{
    static_init();

    DTRACE();

#ifdef BUILD_EXE
    if (const auto &rules = BattleRules::prescribed()) {
        random::seed(seed, rules->random);
        lgr.notice("Battle rules (prescribed): {}")(BattleRules::to_text(*rules));
    }
    else {
        const auto selection = RandomConfiguration::initialize_standalone(seed);
        if (!selection.valid) {
            lgr.error("Invalid RandomGenerator in CFG/standalone.txt; using ParkMiller");
        }
        lgr.notice("Battle random generator: {}")(RandomConfiguration::name(selection.mode));
    }
#else
    random::seed(seed, random::Mode::ParkMiller);
#endif

    g_MatrixHeap = HNew(NULL) CHeap;

#ifdef BUILD_EXE
#ifdef MATRIXGAME_MUSIC
    constexpr bool music = true;
#else
    constexpr bool music = false;  // Without a decoder the package is not used.
#endif
    // Audio follows the packages that opened; a damaged optional package counts as missing.
    const auto audio_packages = Startup::OpenPackages(L"DATA", music);
#else
    CFile::AddPackFile(L"DATA\\robots.pkg");
    CFile::OpenPackFiles();
#endif

    CLoadProgress lp;
    g_LoadProgress = &lp;
    DCP();

    CStorage stor_cfg(g_MatrixHeap);
    bool stor_cfg_present = false;
    std::wstring stor_cfg_name;
    std::wstring conf_file{FILE_CONFIGURATION_LOCATION}; // generate the .dat file path

    if (lang != nullptr)
    {
        conf_file += lang;
        conf_file += L"\\";
    }
    conf_file += FILE_CONFIGURATION;

    if (CFile::FileExist(stor_cfg_name, conf_file.c_str())) {
        if (!stor_cfg.Load(conf_file.c_str())) {
            ERROR_S2(L"Invalid packed configuration: ", conf_file.c_str());
        }
        stor_cfg_present = true;
    }

    g_MatrixData = HNew(g_MatrixHeap) CBlockPar{};
    if (stor_cfg_present)
    {
        stor_cfg.RestoreBlockPar(L"da", *g_MatrixData);
        // stor_cfg.RestoreBlockPar(L"if", *g_MatrixData);
        // g_MatrixData->SaveInTextFile(L"bbb.txt");

        if (CFile::FileExist(stor_cfg_name, L"cfg\\robots\\cfg.txt"))
        {
            CBlockPar *bpc = g_MatrixData->BlockGet(L"Config");
            bpc->LoadFromTextFile(L"cfg\\robots\\cfg.txt");
        }
    }
    else
    {
        g_MatrixData->LoadFromTextFile(L"cfg\\robots\\data.txt");
    }

    // init menu replaces
    // TODO: extract texts automatically from cfg.
    {
        CBlockPar *repl = g_MatrixData->BlockGetAdd(PAR_REPLACE);

        CBlockPar *rr = g_MatrixData->BlockGet(IF_LABELS_BLOCKPAR)->BlockGet(L"Replaces");
        int cnt = rr->ParCount();
        for (int i = 0; i < cnt; ++i) {
            repl->ParAdd(rr->ParGetName(i), rr->ParGet(i));
        }

        if (txt_start) {
            if (txt_start[0] >= '1' && txt_start[0] <= '6') {
                repl->ParSetAdd(PAR_REPLACE_BEGIN_ICON_RACE, std::wstring{txt_start, 1});
                repl->ParSetAdd(PAR_REPLACE_DIFFICULTY, std::wstring{txt_start + 1, 2});
                repl->ParSetAdd(PAR_REPLACE_BEGIN_TEXT, txt_start + 3);
            }
            else {
                repl->ParSetAdd(PAR_REPLACE_BEGIN_ICON_RACE, L"1");
                repl->ParSetAdd(PAR_REPLACE_BEGIN_TEXT, txt_start);
            }
        }
        else {
            repl->ParSetAdd(PAR_REPLACE_BEGIN_ICON_RACE, L"1");
            repl->ParSetAdd(PAR_REPLACE_BEGIN_TEXT, standalone_russian(repl) ? L"\u0412\u043f\u0435\u0440\u0451\u0434!" : L"Go! Go! Go!");
        }

        if (txt_win) {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_WIN, txt_win);
        }
        else {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_WIN, standalone_russian(repl) ? L"\u041f\u043e\u0431\u0435\u0434\u0430!" : L"Victory!");
        }
        if (txt_loss) {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_LOOSE, txt_loss);
        }
        else {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_LOOSE, standalone_russian(repl) ? L"\u041f\u043e\u0440\u0430\u0436\u0435\u043d\u0438\u0435" : L"Defeat");
        }
        if (planet) {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_PLANET, planet);
        }
        else {
            repl->ParSetAdd(PAR_REPLACE_END_TEXT_PLANET, L"");  // the map's name once it is known (statistics title)
        }
    }

    DCP();

    CacheInit();
    DCP();

#ifdef _DEBUG
    g_MatrixData->BlockGet(L"Config")->SaveInTextFile(L"g_ConfigDump.txt");
#endif

    SRobotsSettings settings; // Actual settings
    if (provided_settings != nullptr)
    {
        settings = *provided_settings;
    }
    else
    {
        settings = SRobotsSettings::generate_default_settings();
    }
#ifdef BUILD_EXE
    auto standalone = StandaloneSettings::load();
    for (const auto &key : standalone.invalid) lgr.error("Invalid {} in CFG/standalone.txt; using its default")(key);
    StandaloneSettings::apply_startup(standalone.values, settings);
    std::wstring remembered_map;
#endif

    // Init the 3d engine
#ifdef BUILD_EXE
    L3GInitAsEXE(inst, *g_MatrixData->BlockGet(L"Config"), L"MatrixGame", L"Matrix Game", BattleLoading::has_parent());
    settings.m_ResolutionX = g_ScreenX;
    settings.m_ResolutionY = g_ScreenY;
#else
    L3GInitAsDLL(
                inst,
                *g_MatrixData->BlockGet(L"Config"),
                L"MatrixGame",
                L"Matrix Game",
                wnd,
                settings.FDirect3D,
                settings.FD3DDevice
            );

    g_ScreenX = settings.m_ResolutionX;
    g_ScreenY = settings.m_ResolutionY;
#endif


    g_Render = HNew(g_MatrixHeap) CRenderPipeline;  // prepare pipelines

    // The previous process owns the loading screen during a map-switch handoff.
    // Reveal this window only once battlefield initialization has finished.
    if (!BattleLoading::has_parent()) {
        ShowWindow(g_Wnd, SW_SHOWNORMAL);
        UpdateWindow(g_Wnd);
    }

    DCP();

    g_Config.SetDefaults();
    g_Config.ReadParams();

    g_Config.ApplySettings(&settings);
#ifdef BUILD_EXE
    SoundBridge::initialize_standalone(audio_packages.any());
#endif
    g_Sampler.ApplySettings(&settings);
    SetMaxCameraDistance(settings.m_MaxDistance);


    DCP();

#ifdef BUILD_EXE
    if (!map) {
        const auto configured = g_MatrixData->BlockGet(L"Config")->ParGet(L"Map");
        // A direct relaunch is meaningful only with a still-readable remembered selection.
        const bool resume = StartupMenu::direct_start() && !standalone.values.map.empty() &&
                            MapCatalog::readable(standalone.values.map);
        if (standalone.values.map.empty()) standalone.values.map = configured;
        if (!resume) {
            // A selection removed between processes must reveal the fallback menu,
            // rather than leave it hidden behind the previous process's loader.
            if (BattleLoading::has_parent()) {
                BattleLoading::failed();
                Display::request(standalone.values.display.mode, standalone.values.display.fullscreen);
                ShowWindow(g_Wnd, SW_SHOWNORMAL);
                UpdateWindow(g_Wnd);
            }
            if (!StartupMenu::run(standalone.values)) return;
        }
        if (const auto &lobby = NetBattle::prepared()) {
            // A battle started in the lobby: the host's map, seed, rules and sides, as a command-line one.
            BattleRules::prescribe(lobby->start.rules);
            random::seed(lobby->start.seed, lobby->start.rules.random);
            lgr.notice("Battle rules (lobby): {}")(BattleRules::to_text(lobby->start.rules));
            g_PlayerSide = lobby->start.side;
            g_InitialHumanSides = lobby->start.human_sides;
            map = lobby->start.map.c_str();
        }
        else {
            remembered_map = MapSelection::startup_map(standalone.values);
        }
        if (!BattleLoading::show(g_Wnd, SettingsModel::detect_language(
            g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) == SettingsModel::Language::Russian))
            lgr.warning("Cannot create the loading screen; continuing battle initialization");
    }
    StandaloneSettings::initialize_runtime(standalone.values);
#endif

#ifdef BUILD_EXE
    // Resource balance and bonuses: the prescribed ones, else this person's bonus for their own side
    // (a map named on the command line, as in labs, plays without it).
    if (const auto &prescribed = BattleRules::prescribed()) {
        BattleRules::set_sides(*prescribed);
    }
    else {
        auto mine = StandaloneSettings::battle_rules(standalone.values);
        if (!map) mine.bonus[PLAYER_SIDE - 1] = standalone.values.battle_bonus;
        BattleRules::set_sides(mine);
    }
#endif
    g_MatrixMap = HNew(g_MatrixHeap) CMatrixMapLogic;

    g_MatrixMap->LoadSide(*g_MatrixData->BlockGet(L"Side"));
    // g_MatrixMap->LoadTactics(*g_MatrixData->BlockGet(L"Tactics"));
    g_IFaceList = HNew(g_MatrixHeap) CIFaceList;

    // load new map
    DCP();
    g_MatrixMap->RobotPreload();

    CStorage stor;
    DCP();

    // Process map name
    std::wstring mapname;
    if (map != nullptr)
    {
        if (wcschr(map, '\\') == nullptr)
        {
            // Format map name to include package .pkg path (although the map file can be inside "Matrix/Map/" directory as well)
            mapname = L"Matrix\\Map\\";
            mapname += map;
        }
        else
        {
            mapname = map;
        }
    }
    else
    {
        mapname = g_MatrixData->BlockGet(L"Config")->ParGet(L"Map");
#ifdef BUILD_EXE
        if (!remembered_map.empty()) mapname = remembered_map;
#endif
    }

#ifdef BUILD_EXE
    if (!remembered_map.empty()) MapSelection::begin_map_load();
#endif
    if (!planet) {
        // The host game names the planet of the battle; standalone, the statistics show the map's name.
        std::string error;
        const auto metadata = MapMetadata::load(error);
        auto key = std::filesystem::path(mapname).filename().wstring();
        for (auto &ch : key) ch = wchar(std::towlower(ch));
        const auto found = metadata.find(key);
        g_MatrixData->BlockGet(PAR_REPLACE)->ParSetAdd(PAR_REPLACE_END_TEXT_PLANET,
            found != metadata.end() && !found->second.name.empty() ? MapMetadata::plain_text(found->second.name)
                                                                   : std::filesystem::path(mapname).stem().wstring());
    }
    stor.Load(mapname.c_str());
    DCP();

    if (0 > g_MatrixMap->PrepareMap(stor, mapname))
    {
        ERROR_S(L"Unable to load map. Error happens.");
    }
    DCP();

    std::wstring mapname_lowercase(g_MatrixMap->MapName()); // MapName() is actually the same as "mapname"
    utils::to_lower(mapname_lowercase);
    if (mapname_lowercase.find(L"demo") != std::wstring::npos)
    {
        SETFLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE | MMFLAG_FLYCAM | MMFLAG_FULLAUTO);
    }
    DCP();

    g_MatrixMap->CalcCannonPlace();
    SSpecialBot::LoadAIRobotType(*g_MatrixData->BlockGet(L"AIRobotType"));

    g_LoadProgress->SetCurLP(LP_PREPARININTERFACE);
    g_LoadProgress->InitCurLP(701);

    DCP();
    CBlockPar bpi;
    if (stor_cfg_present)
    {
        stor_cfg.RestoreBlockPar(L"if", bpi);
    }
    else
    {
        bpi.LoadFromTextFile(IF_PATH);

        // CStorage stor(g_CacheHeap);
        // stor.StoreBlockPar(L"if", bpi);
        // stor.StoreBlockPar(L"da", *g_MatrixData);
        // stor.Save(FILE_CONFIGURATION, true);
    }

    g_ConfigHistory = HNew(g_MatrixHeap) CHistory;
    CInterface *pInterface = NULL;

    g_LoadProgress->SetCurLPPos(10);

    CMatrixHint::PreloadBitmaps();

    bool iface_save = false;

    g_PopupHead = (SMenuItemText *)HAlloc(sizeof(SMenuItemText) * MENU_HEAD_ITEMS, g_MatrixHeap);
    for (int i = 0; i < MENU_HEAD_ITEMS; i++) {
        new(&g_PopupHead[i]) SMenuItemText(g_MatrixHeap);
    }

    g_PopupHull = (SMenuItemText *)HAlloc(sizeof(SMenuItemText) * MENU_HULL_ITEMS, g_MatrixHeap);
    for (int i = 0; i < MENU_HULL_ITEMS; i++) {
        new(&g_PopupHull[i]) SMenuItemText(g_MatrixHeap);
    }

    g_PopupWeaponNormal = (SMenuItemText *)HAlloc(sizeof(SMenuItemText) * MENU_WEAPONNORM_ITEMS, g_MatrixHeap);
    for (int i = 0; i < MENU_WEAPONNORM_ITEMS; i++) {
        new(&g_PopupWeaponNormal[i]) SMenuItemText(g_MatrixHeap);
    }

    g_PopupWeaponExtern = (SMenuItemText *)HAlloc(sizeof(SMenuItemText) * MENU_WEAPONEXTERN_ITEMS, g_MatrixHeap);
    for (int i = 0; i < MENU_WEAPONEXTERN_ITEMS; i++) {
        new(&g_PopupWeaponExtern[i]) SMenuItemText(g_MatrixHeap);
    }

    g_PopupChassis = (SMenuItemText *)HAlloc(sizeof(SMenuItemText) * MENU_CHASSIS_ITEMS, g_MatrixHeap);
    for (int i = 0; i < MENU_CHASSIS_ITEMS; i++) {
        new(&g_PopupChassis[i]) SMenuItemText(g_MatrixHeap);
    }

#ifdef BUILD_EXE
    // The startup menu may have changed the size (Settings): the battle renders at the present one, not the
    // one read at the start (else it draws on part of the back buffer and the rest stays black).
    settings.m_ResolutionX = g_ScreenX;
    settings.m_ResolutionY = g_ScreenY;
#endif
    ApplyVideoParams(settings);

    CIFaceMenu::m_MenuGraphics = HNew(g_MatrixHeap) CInterface;

    g_PopupMenu = HNew(g_MatrixHeap) CIFaceMenu;

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_TOP);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);

    g_LoadProgress->SetCurLPPos(100);

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_MINI_MAP);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);

    g_LoadProgress->SetCurLPPos(200);

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_RADAR);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);

    g_LoadProgress->SetCurLPPos(300);

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_BASE);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);

    g_IFaceList->m_BaseX = Float2Int(pInterface->m_xPos);
    g_IFaceList->m_BaseY = Float2Int(pInterface->m_yPos);
    g_LoadProgress->SetCurLPPos(400);

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_MAIN);
    g_IFaceList->SetMainPos(pInterface->m_xPos, pInterface->m_yPos);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);

    g_LoadProgress->SetCurLPPos(500);

    pInterface = HNew(g_MatrixHeap) CInterface;
    iface_save |= pInterface->Load(bpi, IF_HINTS);
    LIST_ADD(pInterface, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface, m_NextInterface);
    g_IFaceList->m_Hints = pInterface;

    g_LoadProgress->SetCurLPPos(600);

    iface_save |= CIFaceMenu::LoadMenuGraphics(bpi);
    ScreenLayout::laid_out(g_ScreenX, g_ScreenY);
    // LIST_ADD(CIFaceMenu::m_MenuGraphics, g_IFaceList->m_First, g_IFaceList->m_Last, m_PrevInterface,
    // m_NextInterface);

    g_IFaceList->ConstructorButtonsInit();

    g_LoadProgress->SetCurLPPos(700);

    g_PopupWeaponExtern[0].text = g_MatrixData->BlockGet(IF_LABELS_BLOCKPAR)->BlockGet(L"Base")->ParGet(L"none");
    g_PopupWeaponNormal[0].text = g_MatrixData->BlockGet(IF_LABELS_BLOCKPAR)->BlockGet(L"Base")->ParGet(L"none");
    g_PopupHead[0].text = g_MatrixData->BlockGet(IF_LABELS_BLOCKPAR)->BlockGet(L"Base")->ParGet(L"none");

    if (g_RangersInterface) {
        g_MatrixMap->m_Transition.BuildTexture();
        // g_D3DD->Reset(&g_D3Dpp);
        g_RangersInterface->m_Begin();
    }

    /*IDirect3DSurface9 * surf;
    g_D3DD->GetRenderTarget(0,&surf);
    if (!(surf==NULL)) g_D3DD->ColorFill(surf, NULL, 0);
    surf->Release();*/

    g_MatrixMap->m_Transition.RenderToPrimaryScreen();

    CMatrixEffect::InitEffects(*g_MatrixData);
    g_MatrixMap->CreatePoolDefaultResources(true);
    g_MatrixMap->InitObjectsLights();

    g_MatrixMap->GetPlayerSide()->Select(BUILDING, g_MatrixMap->GetPlayerSide()->m_ActiveObject);
    g_MatrixMap->m_Cursor.Select(CURSOR_ARROW);

    if (!FLAG(g_MatrixMap->m_Flags, MMFLAG_FULLAUTO))
        g_MatrixMap->EnterDialogMode(TEMPLATE_DIALOG_BEGIN);

    // this code can be safely removed from release : RELEASE_OFF

    // if (iface_save) bpi.SaveInTextFile(IF_PATH, true);
}

void CGame::ApplyVideoParams(SRobotsSettings &set) {
    DTRACE();

    int bpp;
    D3DDISPLAYMODE d3ddm;

    D3DDEVICE_CREATION_PARAMETERS params;
    g_D3DD->GetCreationParameters(&params);
    ASSERT_DX(g_D3D->GetAdapterDisplayMode(params.AdapterOrdinal, &d3ddm));

    if (d3ddm.Format == D3DFMT_X8R8G8B8 || d3ddm.Format == D3DFMT_A8R8G8B8) {
        d3ddm.Format = D3DFMT_A8R8G8B8;
        bpp = 32;
    }
    else {
        bpp = 16;
    }

    bool change_refresh_rate = set.m_RefreshRate != 0 && set.m_RefreshRate != d3ddm.RefreshRate;
    int refresh_rate_required = change_refresh_rate ? set.m_RefreshRate : 0;

    if (set.m_FSAASamples > 16)
        ERROR_S(L"Invalid multisample type");
    _D3DMULTISAMPLE_TYPE expectedMultiSampleType = (_D3DMULTISAMPLE_TYPE)set.m_FSAASamples;  //(m_FSAASamples >> 24);

    ZeroMemory(&g_D3Dpp, sizeof(g_D3Dpp));

#ifdef BUILD_EXE
    INITFLAG(g_Flags, GFLAG_FULLSCREEN, Display::current_mode() == Display::Mode::Exclusive);
#else
    RECT rect;
    GetClientRect(g_Wnd, &rect);
    bool was_in_window_mode = (rect.right != d3ddm.Width || rect.bottom != d3ddm.Height);
    bool now_in_window_mode = was_in_window_mode && (bpp == set.m_BPP) && !change_refresh_rate;

    if (now_in_window_mode) {
        lgr.debug("Apply window mode");
        RESETFLAG(g_Flags, GFLAG_FULLSCREEN);
        g_D3Dpp.Windowed = TRUE;

        RECT r1, r2;
        GetWindowRect(g_Wnd, &r1);
        GetClientRect(g_Wnd, &r2);
        SetWindowPos(g_Wnd, NULL, 0, 0, set.m_ResolutionX + (r1.right - r1.left - r2.right),
                     set.m_ResolutionY + (r1.bottom - r1.top - r2.bottom),
                     SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }
    else {
        lgr.debug("Apply fullscreen mode");
        SETFLAG(g_Flags, GFLAG_FULLSCREEN);
        g_D3Dpp.Windowed = FALSE;

        if (was_in_window_mode) {
            SetWindowLong(g_Wnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            MoveWindow(g_Wnd, 0, 0, set.m_ResolutionX, set.m_ResolutionY, false);
        }
    }

#endif

    g_D3Dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_D3Dpp.BackBufferFormat = (set.m_BPP == 16) ? D3DFMT_R5G6B5 : D3DFMT_A8R8G8B8;
    g_D3Dpp.EnableAutoDepthStencil = TRUE;
    g_D3Dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_D3Dpp.PresentationInterval = set.m_VSync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    g_D3Dpp.FullScreen_RefreshRateInHz = refresh_rate_required;
    g_D3Dpp.BackBufferWidth = set.m_ResolutionX;
    g_D3Dpp.BackBufferHeight = set.m_ResolutionY;
    g_D3Dpp.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
    g_D3Dpp.hDeviceWindow = g_Wnd;
#ifdef BUILD_EXE
    Display::configure_parameters(g_D3Dpp);
#endif

    if (SUCCEEDED(g_D3D->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_D3Dpp.BackBufferFormat,
                                                    g_D3Dpp.Windowed, expectedMultiSampleType, NULL))) {
        g_D3Dpp.MultiSampleType = expectedMultiSampleType;
    }

    SETFLAG(g_Flags, GFLAG_STENCILAVAILABLE);

#ifdef BUILD_EXE
    // The standalone menu has already drawn on this device. Its default-pool
    // buffers and font state must be released before the battlefield reset.
    CInstDraw::MarkAllBuffersNoNeed();
    Text::OnLostDevice();
#endif
    if (D3D_OK != Display::reset_device(g_D3Dpp)) {
        if (bpp == 16)
            g_D3Dpp.AutoDepthStencilFormat = D3DFMT_D16;
        else
            g_D3Dpp.AutoDepthStencilFormat = D3DFMT_D24X8;

        if (D3D_OK != Display::reset_device(g_D3Dpp))
        {
            ERROR_S(L"Sorry, unable to set this params.");
        }
        RESETFLAG(g_Flags, GFLAG_STENCILAVAILABLE);
    }
#ifdef BUILD_EXE
    Text::OnResetDevice();
#endif

    D3DVIEWPORT9 ViewPort;
    ZeroMemory(&ViewPort, sizeof(D3DVIEWPORT9));

    ViewPort.X = 0;
    ViewPort.Y = 0;
    ViewPort.Width = set.m_ResolutionX;
    ViewPort.Height = set.m_ResolutionY;

    ViewPort.MinZ = 0.0f;
    ViewPort.MaxZ = 1.0f;

    ASSERT_DX(g_D3DD->SetViewport(&ViewPort));

    S3D_Default();

    g_D3DD->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, (g_D3Dpp.MultiSampleType != D3DMULTISAMPLE_NONE));

    D3DMATERIAL9 mtrl;
    ZeroMemory(&mtrl, sizeof(D3DMATERIAL9));
    mtrl.Diffuse.r = mtrl.Ambient.r = 1.0f;
    mtrl.Diffuse.g = mtrl.Ambient.g = 1.0f;
    mtrl.Diffuse.b = mtrl.Ambient.b = 1.0f;
    mtrl.Diffuse.a = mtrl.Ambient.a = 1.0f;
    mtrl.Specular.r = 0.5f;
    mtrl.Specular.g = 0.5f;
    mtrl.Specular.b = 0.5f;
    mtrl.Specular.a = 0.5f;
    g_D3DD->SetMaterial(&mtrl);
    g_D3DD->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

    D3DLIGHT9 light;
    ZeroMemory(&light, sizeof(D3DLIGHT9));
    light.Type = D3DLIGHT_DIRECTIONAL;  // D3DLIGHT_POINT;//D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = GetColorR(g_MatrixMap->m_LightMainColorObj);
    light.Diffuse.g = GetColorG(g_MatrixMap->m_LightMainColorObj);
    light.Diffuse.b = GetColorB(g_MatrixMap->m_LightMainColorObj);
    light.Ambient.r = 0.0f;
    light.Ambient.g = 0.0f;
    light.Ambient.b = 0.0f;
    light.Specular.r = GetColorR(g_MatrixMap->m_LightMainColorObj);
    light.Specular.g = GetColorG(g_MatrixMap->m_LightMainColorObj);
    light.Specular.b = GetColorB(g_MatrixMap->m_LightMainColorObj);
    // light.Range       = 0;
    light.Direction = g_MatrixMap->m_LightMain;
    //	light.Direction=D3DXVECTOR3(250.0f,-50.0f,-250.0f);
    //	D3DXVec3Normalize((D3DXVECTOR3 *)(&(light.Direction)),(D3DXVECTOR3 *)(&(light.Direction)));
    ASSERT_DX(g_D3DD->SetLight(0, &light));
    ASSERT_DX(g_D3DD->LightEnable(0, TRUE));
}

void CGame::Deinit(void) {
    DTRACE();

    // A hosted battle can end before the matching key-up or focus event arrives.
    Input::onFocusLost();

    SSpecialBot::ClearAIRobotType();

    g_Config.Clear();

    if (g_Render) {
        HDelete(CRenderPipeline, g_Render, g_MatrixHeap);
        g_Render = NULL;
    }

    MapWindow::shutdown();
    SettingsWindow::shutdown();
    BattleStats::clear();
    ScreenLayout::laid_out(0, 0);
    CMatrixHint::ClearAll();

    if (g_MatrixMap) {
        ASSERT(g_MatrixHeap);

        HDelete(CMatrixMapLogic, g_MatrixMap, g_MatrixHeap);
        g_MatrixMap = NULL;
    }

    if (g_MatrixData) {
        HDelete(CBlockPar, g_MatrixData, NULL);
        g_MatrixData = NULL;
    }

    CMatrixRobot::DestroyPneumaticData();

    if (g_IFaceList) {
        ASSERT(g_MatrixHeap);
        HDelete(CIFaceList, g_IFaceList, g_MatrixHeap);
        g_IFaceList = NULL;
    }

    if (g_ConfigHistory) {
        ASSERT(g_MatrixHeap);
        HDelete(CHistory, g_ConfigHistory, g_MatrixHeap);
        g_ConfigHistory = NULL;
    }

    if (CIFaceMenu::m_MenuGraphics) {
        HDelete(CInterface, CIFaceMenu::m_MenuGraphics, g_MatrixHeap);
        CIFaceMenu::m_MenuGraphics = NULL;
    }

    if (g_PopupMenu) {
        HDelete(CIFaceMenu, g_PopupMenu, g_MatrixHeap);
        g_PopupMenu = NULL;
    }

    if (g_PopupHead) {
        for (int i = 0; i < MENU_HEAD_ITEMS; i++) {
            g_PopupHead[i].text.std::wstring::~wstring();
        }

        HFree(g_PopupHead, g_MatrixHeap);
        g_PopupHead = NULL;
    }

    if (g_PopupHull) {
        for (int i = 0; i < MENU_HULL_ITEMS; i++) {
            g_PopupHull[i].text.std::wstring::~wstring();
        }

        HFree(g_PopupHull, g_MatrixHeap);
        g_PopupHull = NULL;
    }

    if (g_PopupWeaponNormal) {
        for (int i = 0; i < MENU_WEAPONNORM_ITEMS; i++) {
            g_PopupWeaponNormal[i].text.std::wstring::~wstring();
        }

        HFree(g_PopupWeaponNormal, g_MatrixHeap);
        g_PopupWeaponNormal = NULL;
    }

    if (g_PopupWeaponExtern) {
        for (int i = 0; i < MENU_WEAPONEXTERN_ITEMS; i++) {
            g_PopupWeaponExtern[i].text.std::wstring::~wstring();
        }

        HFree(g_PopupWeaponExtern, g_MatrixHeap);
        g_PopupWeaponExtern = NULL;
    }

    if (g_PopupChassis) {
        for (int i = 0; i < MENU_CHASSIS_ITEMS; i++) {
            g_PopupChassis[i].text.std::wstring::~wstring();
        }

        HFree(g_PopupChassis, g_MatrixHeap);
        g_PopupChassis = NULL;
    }

    if (g_Config.m_Labels) {
        for (int i = 0; i < LABELS_LAST; i++) {
            g_Config.m_Labels[i].std::wstring::~wstring();
        }
        HFree(g_Config.m_Labels, g_MatrixHeap);
        g_Config.m_Labels = NULL;
    }

    if (g_Config.m_Descriptions) {
        for (int i = 0; i < DESCRIPTIONS_LAST; i++) {
            g_Config.m_Descriptions[i].std::wstring::~wstring();
        }
        HFree(g_Config.m_Descriptions, g_MatrixHeap);
        g_Config.m_Descriptions = NULL;
    }

    CInstDraw::ClearAll();

    SoundBridge::shutdown();
    CFile::ReleasePackFiles();

    if (g_MatrixHeap) {
        HDelete(CHeap, g_MatrixHeap, NULL);
        g_MatrixHeap = NULL;
    }
}

void CGame::RunGameLoop(CFormMatrixGame *formgame) {
    try {
        g_ExitState = 0;
        FormChange(formgame);

        timeBeginPeriod(1);

        SETFLAG(g_Flags, GFLAG_APPACTIVE);
        RESETFLAG(g_Flags, GFLAG_EXITLOOP);

        L3GRun();
    }
    catch (... /*ExceptionHandler(GetExceptionInformation())*/) {
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
        g_ExitState += 100;
    }
}

void CGame::SaveResult(SRobotGameState *state) {
    state->m_Time = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_TIME);
    state->m_BuildRobot = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_BUILD);
    state->m_KillRobot = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_ROBOT_KILL);
    state->m_BuildTurret = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_TURRET_BUILD);
    state->m_KillTurret = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_TURRET_KILL);
    state->m_KillBuilding = g_MatrixMap->GetPlayerSide()->GetStatValue(STAT_BUILDING_KILL);
}

void CGame::SafeFree(bool end_timer) noexcept {
    try {
        if (end_timer) timeEndPeriod(1);

        Deinit();
    }
    catch (...) {
    }

    try {
        FormChange(NULL);
    }
    catch (...) {
    }

    try {
        if (g_Cache) {
            g_Cache->Clear();
        }
    }
    catch (...) {
    }

    try {
        L3GDeinit();
    }
    catch (...) {
    }

    try {
        CacheDeinit();
    }
    catch (...) {
    }

#ifdef MEM_SPY_ENABLE
    try {
        CMain::BaseDeInit();
    }
    catch (...) {
    }
#endif
}
