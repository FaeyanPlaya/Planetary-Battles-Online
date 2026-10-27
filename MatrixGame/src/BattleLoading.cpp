// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleLoading.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <thread>
#include <cwchar>

namespace BattleLoading {
namespace {
constexpr wchar_t class_name[] = L"MatrixGame_Loading";
constexpr std::wstring_view event_prefix = L"Local\\MatrixGame-Battle-";

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { if (value) CloseHandle(value); }
    void reset() { if (value) CloseHandle(value); value = nullptr; }
};
Handle parent;
bool handoff_screen_owned_elsewhere = false;

// Keep WM_QUIT available to the caller, but do not let it prevent a handoff
// after the old game window has been destroyed. Bound message batches too.
DWORD pump_wait(DWORD count, const HANDLE *handles, DWORD timeout, bool posted_messages = true) {
    const DWORD start = GetTickCount();
    struct Quit {
        bool seen = false;
        int code = 0;
        ~Quit() { if (seen) PostQuitMessage(code); }
    } quit;
    for (;;) {
        const DWORD elapsed = GetTickCount() - start;
        const DWORD remaining = elapsed < timeout ? timeout - elapsed : 0;
        const DWORD result = MsgWaitForMultipleObjectsEx(count, handles, remaining,
            posted_messages ? QS_ALLINPUT : QS_SENDMESSAGE, MWMO_INPUTAVAILABLE);
        if (result != WAIT_OBJECT_0 + count) return result;
        // Screen construction may run inside input dispatch; service synchronous
        // window messages without re-entering game input on the same stack.
        if (!posted_messages) {
            MSG message{};
            PeekMessageW(&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
            if (GetTickCount() - start >= timeout) return WAIT_TIMEOUT;
            continue;
        }
        MSG message{};
        for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) {
            if (message.message == WM_QUIT) { quit.seen = true; quit.code = static_cast<int>(message.wParam); }
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if (GetTickCount() - start >= timeout) return WAIT_TIMEOUT;
    }
}

class Screen {
    Handle started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle finished{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker;
    RECT bounds{};
    bool russian;
    bool created = false;
    unsigned pulse = 0;

    void join_worker() noexcept {
        // Destroying an activated loading window can synchronously notify the
        // game window's thread. Service those messages while the worker exits.
        while (pump_wait(1, &finished.value, handoff_timeout_ms, false) == WAIT_TIMEOUT) {}
        if (worker.joinable()) worker.join();
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto *self = reinterpret_cast<Screen *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Screen *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        if (message == WM_TIMER) {
            ++self->pulse;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            HBRUSH background = CreateSolidBrush(RGB(20, 35, 44));
            FillRect(dc, &area, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(225, 242, 245));
            auto old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            wchar_t text[64]{};
            const wchar_t *label = self->russian ? L"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u0431\u0438\u0442\u0432\u044b" : L"Loading battle";
            std::swprintf(text, 64, L"%ls%.*ls", label, 1 + static_cast<int>(self->pulse % 3), L"...");
            DrawTextW(dc, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, old_font);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_CLOSE || (message == WM_KEYDOWN && wp == VK_ESCAPE)) {
            SetEvent(self->stop.value);
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    void run() noexcept {
        WNDCLASSW type{};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursor(nullptr, IDC_ARROW);
        type.lpszClassName = class_name;
        const ATOM registered = RegisterClassW(&type);
        HWND window = registered ? CreateWindowExW(WS_EX_APPWINDOW, class_name, L"MatrixGame - Loading",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, type.hInstance, this) : nullptr;
        created = window != nullptr;
        if (window) {
            // No hide button: a battle loads in about half a second; Escape or the close box still dismiss it.
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
            SetTimer(window, 1, 350, nullptr);
        }
        SetEvent(started.value);
        if (window) {
            while (MsgWaitForMultipleObjects(1, &stop.value, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
                MSG message{};
                for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i) {
                    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) SetEvent(stop.value);
                    else { TranslateMessage(&message); DispatchMessageW(&message); }
                }
            }
            DestroyWindow(window);
        }
        if (registered) UnregisterClassW(class_name, type.hInstance);
    }
public:
    Screen(HWND source, bool use_russian) : russian(use_russian) {
        if (!started.value || !stop.value || !finished.value) throw std::runtime_error("Cannot create loading-screen events");
        if (!GetWindowRect(source, &bounds)) bounds = {0, 0, 640, 480};
        worker = std::thread([this] { run(); SetEvent(finished.value); });
        const DWORD started_result = pump_wait(1, &started.value, handoff_timeout_ms, false);
        if (started_result != WAIT_OBJECT_0) {
            SetEvent(stop.value);
            join_worker();
            throw std::runtime_error("Loading-screen initialization did not complete");
        }
        if (!created) {
            join_worker();
            throw std::runtime_error("Cannot create the loading screen");
        }
    }
    ~Screen() {
        SetEvent(stop.value);
        if (worker.joinable()) join_worker();
    }
    HANDLE cancellation() const { return stop.value; }
};
std::unique_ptr<Screen> screen;
} // namespace

bool show(HWND game_window, bool russian) noexcept {
    try {
        if (!screen && !handoff_screen_owned_elsewhere && IsWindowVisible(game_window))
            screen = std::make_unique<Screen>(game_window, russian);
        return true;
    }
    catch (...) { return false; }
}
void hide() noexcept { screen.reset(); }

std::wstring event_name() {
    return std::wstring(event_prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
}
bool valid_event_name(std::wstring_view name) {
    if (!name.starts_with(event_prefix) || name.size() <= event_prefix.size() || name.size() > 96) return false;
    return std::all_of(name.begin() + event_prefix.size(), name.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') || ch == L'-';
    });
}
void connect(std::wstring_view name) {
    if (!valid_event_name(name)) throw std::runtime_error("Invalid battle-loading handshake");
    parent.reset();
    handoff_screen_owned_elsewhere = true;
    parent.value = OpenEventW(EVENT_MODIFY_STATE, FALSE, std::wstring(name).c_str());
    // Dismissal can let the old process exit before this process reaches WinMain.
    // An expired event must not turn that harmless race into a startup failure.
    if (!parent.value && GetLastError() != ERROR_FILE_NOT_FOUND)
        throw std::runtime_error("Cannot open the battle-loading handshake");
}
bool has_parent() { return parent.value != nullptr; }
void presented(HRESULT result) {
    if (result != S_OK) return;
    if (parent.value) SetEvent(parent.value);
    parent.reset();
    handoff_screen_owned_elsewhere = false;
    hide();
}
void failed() noexcept {
    if (parent.value) SetEvent(parent.value);
    parent.reset();
    handoff_screen_owned_elsewhere = false;
    hide();
}
WaitResult wait(HANDLE ready, HANDLE process, DWORD timeout_ms, HANDLE cancel) {
    if (!cancel && screen) cancel = screen->cancellation();
    const HANDLE handles[] = {ready, process, cancel};
    const DWORD result = pump_wait(cancel ? 3 : 2, handles, std::min(timeout_ms, handoff_timeout_ms));
    if (result == WAIT_OBJECT_0) return WaitResult::Ready;
    if (result == WAIT_OBJECT_0 + 1) return WaitResult::ChildExited;
    if (cancel && result == WAIT_OBJECT_0 + 2) return WaitResult::Dismissed;
    if (result == WAIT_TIMEOUT) return WaitResult::TimedOut;
    return WaitResult::Failed;
}
} // namespace BattleLoading
