// MatrixGame - licensed under GPLv2 or any later version.
#include "StartupMenu.hpp"
#include "LobbyModel.hpp"
#include "LobbyWindow.hpp"
#include "SettingsWindow.hpp"
#include "SettingsModel.hpp"
#include "MatrixMap.hpp"
#include "MatrixInstantDraw.hpp"
#include "MatrixSampleStateManager.hpp"
#include "Interface/MatrixHint.hpp"
#include "Text/Font.hpp"
#include "DeviceRecovery.hpp"
#include "WindowDisplay.hpp"
#include "Form.hpp"
#include "input.hpp"
#include "LoopPlatform.hpp"
#include "MenuKeyLatch.hpp"
#include "PlayerName.hpp"

#include <stupid_logger.hpp>

namespace StartupMenu {
namespace {
bool skip = false;
bool resources_ready = true;
NetBattle::Options network;  // --lobby-* and the --net-* test options

void release_resources() {
    resources_ready = false;
    CInstDraw::MarkAllBuffersNoNeed();
    Text::OnLostDevice();
}
void restore_resources() {
    Text::OnResetDevice();
    S3D_Default();
    resources_ready = true;
}

// Rejoin shows only while a network battle this game lost the connection to may still go on.
enum Item { Rejoin, NewBattle, HostBattle, JoinBattle, Name, Settings, Exit, ItemCount };
constexpr int item_x = 50;
constexpr int item_width = 260;
constexpr int first_item_y = 95;
constexpr int item_step = 35;
constexpr int menu_width = 360;
constexpr int menu_height = 340;

class Form final : public CForm {
    StandaloneSettings::Values &values;
    CMatrixHint *menu = nullptr;
    bool ready_to_draw = false;
    int item = NewBattle;
    bool russian = false;
    bool host_failed = false;  // the last Host could not open a lobby
    bool opened_network = false;
    bool can_rejoin = false;
    MenuKeyLatch actions;

    int first_item() const { return can_rejoin ? Rejoin : NewBattle; }
    int shown_items() const { return ItemCount - first_item(); }
    int item_y(int i) const { return first_item_y + (i - first_item()) * item_step; }

    const wchar_t *text(const wchar_t *english, const wchar_t *russian_text) const { return russian ? russian_text : english; }

    void release_menu() {
        if (menu) menu->Release();
        menu = nullptr;
    }
    void show_menu() {
        if (!menu) {
            using SettingsModel::text_element;
            const int height = menu_height + (can_rejoin ? item_step : 0);
            auto markup = std::to_wstring(SettingsModel::window_border) + L"|_BORDER:0:0:0:0|_FONT:Font.Heavy14|_WIDTH:" +
                          std::to_wstring(menu_width) +
                          L"|_HEIGHT:" + std::to_wstring(height) + L"|_TEXTP:L|_TEXT: |_TEXTP:B";
            markup += text_element(L"MatrixGame", L"Font.2Ranger", 0xff000000, 0, 35, menu_width, 1);
            const std::wstring nick = LobbyWindow::player_name(network);
            const std::wstring name_label = std::wstring(text(L"( Name: ", L"( \u041d\u0438\u043a: ")) + (nick.empty() ? L"-" : nick) + L" )";
            const wchar_t *labels[ItemCount] = {
                text(L"( Back to the battle )", L"( Вернуться в бой )"),
                text(L"( New battle )", L"( \u041d\u043e\u0432\u0430\u044f \u0431\u0438\u0442\u0432\u0430 )"),
                text(L"( Host a network battle )", L"( \u0421\u043e\u0437\u0434\u0430\u0442\u044c \u0441\u0435\u0442\u0435\u0432\u043e\u0439 \u0431\u043e\u0439 )"),
                text(L"( Join a network battle )", L"( \u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0438\u0442\u044c\u0441\u044f \u043a \u0431\u043e\u044e )"),
                name_label.c_str(),
                text(L"( Settings )", L"( \u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 )"),
                text(L"( Exit )", L"( \u0412\u044b\u0445\u043e\u0434 )"),
            };
            for (int i = first_item(); i < ItemCount; ++i)
                markup += text_element(labels[i], L"Font.Heavy14", i == item ? 0xff0b5fa5 : 0xff000000, item_x, item_y(i),
                                       item_width, 1);
            if (host_failed)
                markup += text_element(LobbyModel::join_error_text(russian ? LobbyModel::Language::Russian : LobbyModel::Language::English,
                                                                   LobbyModel::JoinError::Busy),
                                       L"Font.Heavy13", 0xffb01818, 10, item_y(ItemCount) - 5, menu_width - 20, 1);
            menu = CMatrixHint::Build(markup, static_cast<CBlockPar *>(nullptr));
        }
        menu->Show((g_ScreenX - menu->m_Width) / 2, (g_ScreenY - menu->m_Height) / 2);
    }
    void select(int next) {
        if (item == next) return;
        item = next;
        release_menu();
        show_menu();
    }
    void activate() {
        host_failed = false;
        release_menu();
        switch (item) {
            case Exit: SETFLAG(g_Flags, GFLAG_EXITLOOP); break;
            case Rejoin:
                // The game starts again and replays the battle (NetBattle).
                NetBattle::request_rejoin(LobbyWindow::player_name(network));
                SETFLAG(g_Flags, GFLAG_EXITLOOP);
                break;
            case NewBattle: LobbyWindow::open_local(values, network.lobby_local ? network : without_lobby()); break;
            case HostBattle: open_host(network.lobby ? network : without_lobby()); break;
            case JoinBattle: LobbyWindow::open_join(values, network.lobby ? network : without_lobby()); break;
            case Name: LobbyWindow::open_name(false); break;
            case Settings: SettingsWindow::open_startup(values); break;
        }
    }
    // Test options of the command line without its lobby address.
    NetBattle::Options without_lobby() const {
        NetBattle::Options options = network;
        options.lobby = false;
        return options;
    }
    void open_host(const NetBattle::Options &options) {
        if (!LobbyWindow::open_host(values, options)) {
            host_failed = true;
            show_menu();
        }
    }
    bool windows_active() const { return LobbyWindow::active() || SettingsWindow::startup_active(); }
    int item_at(int x, int y) const {
        x -= menu->m_PosX;
        y -= menu->m_PosY;
        if (x < item_x || x >= item_x + item_width) return -1;
        for (int i = first_item(); i < ItemCount; ++i)
            if (y >= item_y(i) && y < item_y(i) + 20) return i;
        return -1;
    }
public:
    explicit Form(StandaloneSettings::Values &settings) : values(settings) {
        m_Name = L"Startup menu";
        russian = SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
                  SettingsModel::Language::Russian;
        can_rejoin = NetBattle::rejoin_ticket().has_value();
        item = first_item();
    }
    ~Form() {
        if (g_FormCur == this) FormChange(nullptr);
        LobbyWindow::shutdown();
        SettingsWindow::shutdown();
        release_menu();
    }
    void Enter() override {
        S3D_Default();
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        if (network.lobby && !opened_network) {
            opened_network = true;
            if (network.lobby_test_size.first > 0)  // the device changes in the next frames, as after Settings
                Display::request_size(network.lobby_test_size.first, network.lobby_test_size.second);
            if (network.lobby_local) LobbyWindow::open_local(values, network);
            else if (network.role == NetBattle::Role::Host) open_host(network);
            else LobbyWindow::open_join(values, network);
            if (LobbyWindow::active()) return;
        }
        // The first start: a name before anything else.
        if (!network.lobby && PlayerName::load().empty()) {
            LobbyWindow::open_name(true);
            if (LobbyWindow::active()) return;
        }
        show_menu();
    }
    void Leave() override { ClipCursor(nullptr); Input::onFocusLost(); }
    void Takt(int) override {
        ready_to_draw = false;
        LobbyWindow::poll();  // people keep joining whatever the device does
        if (Display::apply_pending(resources_ready, release_resources, restore_resources)) return;
        if (Rendering::RecoverDevice(g_D3DD->TestCooperativeLevel(), resources_ready, release_resources,
                [] { return Display::reset_device(g_D3Dpp); }, restore_resources,
                [](auto duration) { MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(duration.count()), QS_ALLINPUT); }))
            return;
        LobbyWindow::takt();
        if (LobbyWindow::started()) return;
        if (!windows_active()) show_menu();
        ready_to_draw = true;
    }
    void Draw() override {
        if (!ready_to_draw) return;
        CInstDraw::DrawFrameBegin();
        // The initial menu device has no depth buffer until battle video setup.
        ASSERT_DX(g_D3DD->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff14232c, 1.0f, 0));
        ASSERT_DX(g_D3DD->BeginScene());
        CMatrixHint::DrawAll();
        ASSERT_DX(g_D3DD->EndScene());
        Rendering::CheckPresentResult(g_D3DD->Present(nullptr, nullptr, nullptr, nullptr));
        // The static menu does not need an uncapped render loop.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
    void MouseMove(int x, int y) override {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        SettingsWindow::startup_mouse_move(x, y);
        if (!menu || windows_active()) return;
        if (const int at = item_at(x, y); at >= 0) select(at);
    }
    void MouseKey(ButtonStatus status, int key, int x, int y) override {
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        if (SettingsWindow::startup_active()) {
            if (status == B_DOWN && key == VK_LBUTTON) SettingsWindow::startup_mouse_down(x, y);
            return;
        }
        if (LobbyWindow::active()) {
            if ((status == B_DOWN || status == B_DOUBLE) && key == VK_LBUTTON) LobbyWindow::mouse_down(x, y);
            if (status == B_WHEEL) LobbyWindow::mouse_wheel(key, x, y);
            return;
        }
        if (status != B_DOWN || key != VK_LBUTTON || !menu) return;
        if (const int at = item_at(x, y); at >= 0) {
            select(at);
            activate();
        }
    }
    void Keyboard(bool down, uint8_t key) override {
        if (!resources_ready || g_D3DD->TestCooperativeLevel() != D3D_OK) return;
        if (down) Input::onKeyDown(key); else Input::onKeyUp(key);
        const bool accepted = actions.accept(key, down);
        if (SettingsWindow::startup_keyboard(key, down) || LobbyWindow::keyboard(key, down) || !accepted)
            return;
        const int first = first_item(), count = shown_items();
        if (key == VK_UP) select(first + (item - first + count - 1) % count);
        else if (key == VK_DOWN || key == VK_TAB) select(first + (item - first + 1) % count);
        else if (key == VK_RETURN) activate();
        else if (key == 'N') { select(NewBattle); activate(); }
        else if (key == VK_ESCAPE) SETFLAG(g_Flags, GFLAG_EXITLOOP);
    }
    void SystemEvent(ESysEvent event) override {
        if (event == SYSEV_DEACTIVATING && resources_ready) release_resources();
        if (event == SYSEV_DEACTIVATING || event == SYSEV_INPUT_RESET) {
            Input::onFocusLost();
            actions = {};
            LobbyWindow::reset_input();
            SettingsWindow::startup_reset_input();
        }
    }
};
}  // namespace

void set_direct_start(bool enabled) { skip = enabled; }
bool direct_start() { return skip; }
void set_network(const NetBattle::Options &options) { network = options; }

bool run(StandaloneSettings::Values &values) {
    ASSERT(g_MatrixMap == nullptr);
    lgr.notice("Opening the startup menu before battlefield initialization");
    resources_ready = true;
    Form form(values);
    FormChange(&form);
    // A lobby goes on in the background (people join while the host is in another window), and its
    // window keeps showing them; without a lobby the loop only waits there.
    const Loop::Platform platform{
        nullptr,
        [](void *) { return Loop::Clock::now(); },
        [](void *, MSG &message) {
            if (LobbyWindow::active()) SETFLAG(g_Flags, GFLAG_APPACTIVE);
            return PeekMessage(&message, NULL, 0, 0, PM_REMOVE) != FALSE;
        },
        [](void *, const MSG &message) { DispatchMessage(&message); },
        [](void *) {
            LobbyWindow::poll();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }};
    L3GRun(platform);
    const bool start = LobbyWindow::started() && !FLAG(g_Flags, GFLAG_APPCLOSE);
    RESETFLAG(g_Flags, GFLAG_EXITLOOP);
    return start;
}
}  // namespace StartupMenu
