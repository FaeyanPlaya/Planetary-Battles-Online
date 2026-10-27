// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "Texture.hpp"
#include "3g.hpp"
#include "Helper.hpp"
#include "GraphicsReferences.hpp"
#include "WindowDisplay.hpp"
#include "LoopPlatform.hpp"
#include "../../MatrixGame/src/MatrixSampleStateManager.hpp"

#include "CBlockPar.hpp"
#include "CException.hpp"
#include "CReminder.hpp"

#include <utils.hpp>
#include <fps_counter.hpp>
#include <stupid_logger.hpp>

#include <fstream>
#include <chrono>
#include <thread>
#include <exception>
#include <utility>

#include <stdio.h>

#ifdef __GNUC__
    #include "dxerr9.h"
    #define DXGetErrorStringW DXGetErrorString9W
    #define DXGetErrorDescriptionW DXGetErrorDescription9W
#else
    #include "dxerr.h"
#endif // __GNUC__

HINSTANCE g_HInst = 0;
IDirect3D9 *g_D3D = NULL;
IDirect3DDevice9 *g_D3DD = NULL;
namespace {
Graphics::Reference<IDirect3D9> owned_direct3d;
Graphics::Reference<IDirect3DDevice9> owned_device;
// Window callbacks must return through USER32 without unwinding C++ frames.
// Keep the original exception until the owning game loop can rethrow it.
std::exception_ptr callback_failure;
bool callback_failed = false;

void rethrow_callback_failure() {
    if (auto failure = std::exchange(callback_failure, {}))
        std::rethrow_exception(failure);
}
}
D3DCAPS9 g_D3DDCaps;
ATOM g_WndA = 0;
HWND g_Wnd = 0;
bool g_WndExtern = false;
DWORD g_WndOldProg = 0;
std::wstring g_WndClassName;
int g_ScreenX = 0, g_ScreenY = 0;
D3DPRESENT_PARAMETERS g_D3Dpp;

#define SMOOTH_COUNT 16

DWORD g_Flags = 0;
bool g_ScriptedInput = false;

int g_MaxFPS;
int g_DrawFPS;

int g_AvailableTexMem;

#ifdef DO_SMART_COLOROPS

STextureStageOp g_ColorOp[8] = {
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}};
STextureStageOp g_AlphaOp[8] = {
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT},
        {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}, {D3DTOP_DISABLE, D3DTA_CURRENT, D3DTA_CURRENT}};
#endif

static D3DGAMMARAMP g_StoreRamp0;
static D3DGAMMARAMP g_StoreRamp1;

LRESULT CALLBACK L3G_WndProc(HWND, UINT, WPARAM, LPARAM);

#ifdef _DEBUG
#include "D3DControl.hpp"

int D3DResource::m_IB_cnt;
int D3DResource::m_VB_cnt;
int D3DResource::m_TEX_cnt;
DWORD D3DResource::m_UID_current;
D3DResource *D3DResource::m_First;
D3DResource *D3DResource::m_Last;

void D3DResource::StaticInit(void) {
    m_IB_cnt = 0;
    m_VB_cnt = 0;
    m_TEX_cnt = 0;
    m_UID_current = 0;
    m_First = NULL;
    m_Last = NULL;
}
void D3DResource::Dump(D3DResType t) {
    std::string buf{"D3D Dump\n"};
    D3DResource *el = m_First;
    while (el)
    {
        if (t == el->m_Type)
        {
            if (buf.length() < 65000)
            {
                buf += utils::format("%u : %s - %i\n", el->m_UID, el->m_File, el->m_Line);
            }
        }
        el = el->m_Next;
    }

    // MessageBox(g_Wnd, buf.c_str(), "D3D Dump", MB_ICONINFORMATION);

    std::ofstream out("debug_dump.txt");
    out << buf;
}
#endif

std::wstring CExceptionD3D::Info() const
{
    return CException::Info() +
           utils::format(
               L"Text: {%ls} %s",
               DXGetErrorStringW(m_Error),
               DXGetErrorDescriptionW(m_Error)).c_str();
}

void L3GInitAsEXE(HINSTANCE hinst, [[maybe_unused]] CBlockPar& bpcfg, const wchar* sysname, const wchar* captionname,
                  bool defer_exclusive) {

    L3GDeinit();

    g_HInst = hinst;
    g_Wnd = NULL;
    g_WndExtern = false;

    auto selection = Display::load();
    if (!selection.valid) lgr.error("Invalid display settings in CFG/standalone.txt; using 1024x768 Windowed");
    if (selection.settings.width == 0 || selection.settings.height == 0) {  // Auto
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        Display::Size screen{0, 0};
        if (GetMonitorInfo(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info))
            screen = {info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top};
        const auto size = Display::auto_size(screen);
        selection.settings.width = size.width;
        selection.settings.height = size.height;
        lgr.notice("Display: automatic size {}x{} for a {}x{} screen")(size.width, size.height, screen.width, screen.height);
    }
    // A relaunch keeps the previous process's loading screen on the desktop.
    // Acquire exclusive mode only after the new battlefield is ready to render.
    if (defer_exclusive && selection.settings.mode == Display::Mode::Exclusive)
        selection.settings.mode = Display::Mode::Windowed;
    Display::initialize(selection.settings);
    g_ScreenX = selection.settings.width;
    g_ScreenY = selection.settings.height;

    g_WndClassName = sysname;
    g_WndClassName += L"_wc";
    std::string classname{utils::from_wstring(g_WndClassName)};

    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = (WNDPROC)L3G_WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = g_HInst;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = 0;  //(HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = classname.c_str();
    wcex.hIconSm = NULL;
    if (!(g_WndA = RegisterClassEx(&wcex)))
        ERROR_E;

    g_Wnd = CreateWindow(classname.c_str(), utils::from_wstring(captionname).c_str(),
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         0, 0, 640, 480, NULL, NULL, g_HInst, NULL);
    if (!g_Wnd) ERROR_S(L"Cannot create the game window");

    g_D3D = Direct3DCreate9(D3D_SDK_VERSION);
    owned_direct3d.adopt(g_D3D);
    if (!g_D3D)
    {
        ERROR_S(L"Direct3DCreate9 failed");
    }

    auto startup_mode = Display::current_mode();
    if (startup_mode == Display::Mode::Exclusive && !Display::supports_exclusive()) {
        lgr.error("Exclusive display mode {}x{} is unavailable; starting Windowed")(g_ScreenX, g_ScreenY);
        startup_mode = Display::Mode::Windowed;
    }
    Display::apply_window(startup_mode);
    lgr.notice("Display mode: {}; rendering {}x{}")(Display::name(startup_mode), g_ScreenX, g_ScreenY);

    D3DPRESENT_PARAMETERS d3dpp{};
    d3dpp.EnableAutoDepthStencil = 0;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    Display::configure_parameters(d3dpp);

    auto cd_res =
        g_D3D->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            g_Wnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
            &d3dpp,
            &g_D3DD
        );
    owned_device.adopt(g_D3DD);

    switch(cd_res)
    {
        case D3D_OK: break;
        case D3DERR_DEVICELOST:
            ERROR_S(L"CreateDevice failed: D3DERR_DEVICELOST");
        case D3DERR_INVALIDCALL:
            ERROR_S(L"CreateDevice failed: D3DERR_INVALIDCALL");
        case D3DERR_NOTAVAILABLE:
            ERROR_S(L"CreateDevice failed: D3DERR_NOTAVAILABLE");
        case D3DERR_OUTOFVIDEOMEMORY:
            ERROR_S(L"CreateDevice failed: D3DERR_OUTOFVIDEOMEMORY");
        default:
            throw CExceptionD3D(__FILE__, __LINE__, cd_res);
    }

    SetWindowLongPtr(g_Wnd, GWL_WNDPROC, uintptr_t((WNDPROC)L3G_WndProc));

    IDirect3DSurface9 *surf = nullptr;
    FAILED_DX(g_D3DD->GetRenderTarget(0, &surf));
    if (!(surf == NULL))
        g_D3DD->ColorFill(surf, NULL, 0);
    surf->Release();

    g_D3Dpp = {};
    Display::configure_parameters(g_D3Dpp);
    g_D3Dpp.EnableAutoDepthStencil = TRUE;
    g_D3Dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    g_D3Dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    g_D3Dpp.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;

    // Set default settings
    UINT AdapterToUse = D3DADAPTER_DEFAULT;
    D3DDEVTYPE DeviceType = D3DDEVTYPE_HAL;

    SETFLAG(g_Flags, GFLAG_STENCILAVAILABLE);

    ASSERT_DX(g_D3DD->GetDeviceCaps(&g_D3DDCaps));

    INITFLAG(g_Flags, GFLAG_GAMMA, g_D3DDCaps.Caps2 & D3DCAPS2_CANCALIBRATEGAMMA);

    if (FLAG(g_Flags, GFLAG_GAMMA))
        g_D3DD->GetGammaRamp(0, &g_StoreRamp0);

    g_AvailableTexMem = g_D3DD->GetAvailableTextureMem() / (1024 * 1024);

#ifdef DO_SMART_COLOROPS
    for (int i = 0; i < 8; i++) {
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLOROP, D3DTOP_DISABLE));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLORARG1, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLORARG2, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAARG1, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT));
    }
#endif
}

void L3GInitAsDLL(
    HINSTANCE hinst,
    [[maybe_unused]] CBlockPar& bpcfg,
    [[maybe_unused]] const wchar* sysname,
    [[maybe_unused]] const wchar* captionname,
    HWND hwnd,
    uintptr_t FDirect3D,
    uintptr_t FD3DDevice)
{
    // RECT tr;

    L3GDeinit();

    g_HInst = hinst;
    g_Wnd = hwnd;
    g_WndExtern = true;

    g_D3D = (IDirect3D9 *)FDirect3D;
    g_D3DD = (IDirect3DDevice9 *)FD3DDevice;

    g_WndOldProg = GetWindowLong(g_Wnd, GWL_WNDPROC);
    if (g_WndOldProg == 0)
        ERROR_E;
    if (SetWindowLongPtr(g_Wnd, GWL_WNDPROC, uintptr_t((WNDPROC)L3G_WndProc)) == 0)
        ERROR_E;

    /*IDirect3DSurface9 * surf;
    g_D3DD->GetRenderTarget(0,&surf);
    if (!(surf==NULL)) g_D3DD->ColorFill(surf, NULL, 0);
    surf->Release();*/

    ASSERT_DX(g_D3DD->GetDeviceCaps(&g_D3DDCaps));

    INITFLAG(g_Flags, GFLAG_GAMMA, g_D3DDCaps.Caps2 & D3DCAPS2_CANCALIBRATEGAMMA);

    if (FLAG(g_Flags, GFLAG_GAMMA))
        g_D3DD->GetGammaRamp(0, &g_StoreRamp0);

    g_AvailableTexMem = g_D3DD->GetAvailableTextureMem() / (1024 * 1024);

#ifdef DO_SMART_COLOROPS
    for (int i = 0; i < 8; i++) {
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLOROP, D3DTOP_DISABLE));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLORARG1, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_COLORARG2, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAARG1, D3DTA_CURRENT));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT));
    }
#endif
}

void L3GDeinit() {
    if (g_D3DD && FLAG(g_Flags, GFLAG_GAMMA)) {
        g_D3DD->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &g_StoreRamp0);
        g_D3DD->SetGammaRamp(1, D3DSGR_NO_CALIBRATION, &g_StoreRamp1);
    }

#if (defined _DEBUG) && !(defined _RELDEBUG)
    CHelper::ClearAll();
#endif

    owned_device.release(g_D3DD);
    owned_direct3d.release(g_D3D);
    RESETFLAG(g_Flags, GFLAG_GAMMA);
    ZeroMemory(&g_D3DDCaps, sizeof(D3DCAPS9));

    if (g_WndA)
    {
        if (g_Wnd)
        {
            DestroyWindow(g_Wnd);
            g_Wnd = 0;
        }
        UnregisterClass(utils::from_wstring(g_WndClassName).c_str(), g_HInst);
        g_WndA = 0;
    }
    if (g_WndExtern) {
        SetWindowLong(g_Wnd, GWL_WNDPROC, g_WndOldProg);
    }
    g_Wnd = nullptr;
    g_WndOldProg = 0;
    g_WndExtern = false;
    Display::shutdown();
    callback_failure = nullptr;
    callback_failed = false;
}

bool processInput(UINT message, WPARAM wParam, LPARAM lParam);

int L3GRun()
{
    const Loop::Platform platform{
        nullptr,
        [](void *) { return Loop::Clock::now(); },
        [](void *, MSG &message) { return PeekMessage(&message, NULL, 0, 0, PM_REMOVE) != FALSE; },
        [](void *, const MSG &message) { DispatchMessage(&message); },
        [](void *) { MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT); }
    };
    return L3GRun(platform);
}

int L3GRun(const Loop::Platform &platform)
{
    rethrow_callback_failure();

    constexpr auto to_milliseconds = [](auto dur) { return std::chrono::duration_cast<std::chrono::milliseconds>(dur); };

    // int smooth[SMOOTH_COUNT];
    // int smooths = SMOOTH_COUNT * 10;
    // int smp = 0;
    // for (int i = 0; i < SMOOTH_COUNT; ++i)
    //     smooth[i] = 10;

    fps_counter<100> fps;
    MSG msg;

    auto prev_takt = platform.now(platform.context);

    while (true)
    {
        while (true)
        {
            // The native poll may itself deliver sent (synchronous) messages.
            const bool available = platform.poll(platform.context, msg);
            rethrow_callback_failure();
            if (!available) break;
            platform.dispatch(platform.context, msg);
            rethrow_callback_failure();
        }
        if (FLAG(g_Flags, GFLAG_APPCLOSE) || FLAG(g_Flags, GFLAG_EXITLOOP))
        {
            break;
        }

        if (!FLAG(g_Flags, GFLAG_APPACTIVE) || !g_FormCur)
        {
            platform.wait(platform.context);
            continue;
        }

        auto cur_takt = platform.now(platform.context);
        auto delta_time = (cur_takt - prev_takt);
        prev_takt = cur_takt;

        if (platform.advance)
        {
            while (platform.advance(platform.context))
            {
                rethrow_callback_failure();
#ifdef _DEBUG
                SETFLAG(g_Flags, GFLAG_TAKTINPROGRESS);
#endif
                lgr.add_ticks(platform.step_ms);
                SRemindCore::Takt(platform.step_ms);
                rethrow_callback_failure();
                g_FormCur->Takt(platform.step_ms);
#ifdef _DEBUG
                RESETFLAG(g_Flags, GFLAG_TAKTINPROGRESS);
#endif
                rethrow_callback_failure();
            }
            rethrow_callback_failure();
            g_FormCur->Draw();
            rethrow_callback_failure();
#if (defined _DEBUG) && !(defined _RELDEBUG)
            CHelper::AfterDraw();
#endif
            fps++;
            g_DrawFPS = fps.count();
            g_AvailableTexMem = g_D3DD->GetAvailableTextureMem() / (1024 * 1024);
            continue;
        }

        if (FLAG(g_Flags, GFLAG_4SPEED))
        {
            delta_time *= 4;
        }

        int delta = std::min(100LL, to_milliseconds(delta_time).count());

        // TODO: bring smoothness back?
        // smooths -= smooth[smp];
        // smooths += delta;
        // smooth[smp] = delta;
        // smp = (smp + 1) & (SMOOTH_COUNT - 1);

        // delta = smooths / SMOOTH_COUNT;

#ifdef _DEBUG
        SETFLAG(g_Flags, GFLAG_TAKTINPROGRESS);
#endif

        lgr.add_ticks(delta);
        SRemindCore::Takt(delta);
        rethrow_callback_failure();
        g_FormCur->Takt(delta);
#ifdef _DEBUG
        RESETFLAG(g_Flags, GFLAG_TAKTINPROGRESS);
#endif
        rethrow_callback_failure();

        // TODO: maybe add back FPS limit?
        g_FormCur->Draw();
        rethrow_callback_failure();
#if (defined _DEBUG) && !(defined _RELDEBUG)
        CHelper::AfterDraw();
#endif
        fps++;

        g_DrawFPS = fps.count();

        g_AvailableTexMem = g_D3DD->GetAvailableTextureMem() / (1024 * 1024);
    }

    return 1;
}

/**
 * @brief Calls corresponding input handlers on the game's form.
 *
 * @return true if input should be propagated to the OS for additional handling
 *          (for example if we don't handle such messages), false otherwise
 */
bool processInput(UINT message, WPARAM wParam, LPARAM lParam)
{
    int mouse_y = static_cast<int16_t>(HIWORD(lParam));
    int mouse_x = static_cast<int16_t>(LOWORD(lParam));
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) Display::map_mouse(message, mouse_x, mouse_y);
    int16_t hi_wparam = HIWORD(wParam);

    bool propagate = false;

    switch (message)
    {
        case WM_MOUSEWHEEL:
            g_FormCur->MouseKey(B_WHEEL, hi_wparam / 120, mouse_x, mouse_y);
            break;
        case WM_MOUSEMOVE:
            if (Display::enabled()) {
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, g_Wnd, 0};
                TrackMouseEvent(&tracking);
            }
            g_FormCur->MouseMove(mouse_x, mouse_y);
            break;
        case WM_MOUSELEAVE:
            if (Display::enabled()) g_FormCur->SystemEvent(SYSEV_POINTER_LEFT);
            break;
        case WM_LBUTTONDOWN:
            g_FormCur->MouseKey(B_DOWN, VK_LBUTTON, mouse_x, mouse_y);
            break;
        case WM_LBUTTONDBLCLK:
            g_FormCur->MouseKey(B_DOUBLE, VK_LBUTTON, mouse_x, mouse_y);
            break;
        case WM_RBUTTONDOWN:
            g_FormCur->MouseKey(B_DOWN, VK_RBUTTON, mouse_x, mouse_y);
            break;
        case WM_RBUTTONDBLCLK:
            g_FormCur->MouseKey(B_DOUBLE, VK_RBUTTON, mouse_x, mouse_y);
            break;
        case WM_MBUTTONDOWN:
            g_FormCur->MouseKey(B_DOWN, VK_MBUTTON, mouse_x, mouse_y);
            break;
        case WM_LBUTTONUP:
            g_FormCur->MouseKey(B_UP, VK_LBUTTON, mouse_x, mouse_y);
            break;
        case WM_RBUTTONUP:
            g_FormCur->MouseKey(B_UP, VK_RBUTTON, mouse_x, mouse_y);
            break;
        case WM_MBUTTONUP:
            g_FormCur->MouseKey(B_UP, VK_MBUTTON, mouse_x, mouse_y);
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            g_FormCur->Keyboard(true, wParam);
            propagate = true;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            g_FormCur->Keyboard(false, wParam);
            propagate = true;
            break;
        default:
            propagate = true;
            break;
    }

    return propagate;
}

/**
 * @brief Window message callback handler.
 *
 * Called either by DispatchMessage() or by other means.
 */
static LRESULT window_message(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (Display::changing()) {
        // Reset and style changes can synchronously dispatch activation messages.
        // Do not enter graphics or gameplay handlers until the transition returns.
        if (message == WM_ACTIVATEAPP) {
            INITFLAG(g_Flags, GFLAG_APPACTIVE, wParam != 0);
            if (!wParam) ClipCursor(nullptr);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    if (g_FormCur && FLAG(g_Flags, GFLAG_APPACTIVE) && Display::handle_shortcut(message, wParam, lParam)) return 0;
    if (g_ScriptedInput && (message == WM_ACTIVATEAPP || message == WM_MOUSELEAVE)) {
        // Focus changes would reset held keys and selection at a moment no replay can reproduce.
        SETFLAG(g_Flags, GFLAG_APPACTIVE);
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    switch (message)
    {
        case WM_PAINT:
            break;
        // case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
        {
            if (!wParam) Display::focus_lost();
            if (FLAG(g_Flags, GFLAG_KEEPALIVE))
            {
                if (wParam == 0 && g_FormCur) {
                    g_FormCur->SystemEvent(SYSEV_INPUT_RESET);
                }
                // don't minimize or pause the game when keepalive flag is set
                break;
            }
            if (wParam != 0)
            {
                SETFLAG(g_Flags, GFLAG_APPACTIVE);
                if (FLAG(g_Flags, GFLAG_FULLSCREEN))
                {
                    ShowWindow(g_Wnd, SW_MAXIMIZE);
                }
                else
                {
                    ShowWindow(g_Wnd, SW_RESTORE);
                }
                // if (g_D3DD->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {g_D3DD->Reset(&g_D3Dpp);}
                if (g_FormCur)
                {
                    g_FormCur->SystemEvent(SYSEV_ACTIVATED);
                }
            }
            else
            {
                RESETFLAG(g_Flags, GFLAG_APPACTIVE);
                if (g_FormCur)
                {
                    g_FormCur->SystemEvent(SYSEV_DEACTIVATING);
                }

                if (!Display::enabled() || Display::current_mode() == Display::Mode::Exclusive)
                    ShowWindow(g_Wnd, SW_MINIMIZE);
                ClipCursor(NULL);
            }
            break;
        }
        case WM_SETCURSOR:
            return true;  // prevent Windows from setting cursor to window class cursor
        case WM_DESTROY:
        case WM_CLOSE:
        case WM_QUIT:
            SETFLAG(g_Flags, GFLAG_APPCLOSE);
            //          PostQuitMessage(0);
            return 0;
    }

    if (g_FormCur && FLAG(g_Flags, GFLAG_APPACTIVE))
    {
        bool propagate = processInput(message, wParam, lParam);
        if (!propagate)
        {
            return 0;
        }
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK L3G_WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (callback_failed) return DefWindowProc(hWnd, message, wParam, lParam);
    try {
        return window_message(hWnd, message, wParam, lParam);
    }
    catch (...) {
        if (!callback_failure) callback_failure = std::current_exception();
        callback_failed = true;
        SETFLAG(g_Flags, GFLAG_EXITLOOP);
        return 0;
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void S3D_Default() {
    D3DXMATRIX mWorld;
    D3DXMatrixIdentity(&mWorld);
    ASSERT_DX(g_D3DD->SetTransform(D3DTS_WORLD, &mWorld));

    float fBias = -1.0f;

    for (int i = 0; i < 8; i++) {
        ASSERT_DX(g_Sampler.SetState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
        ASSERT_DX(g_Sampler.SetState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));

        ASSERT_DX(g_D3DD->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP));
        ASSERT_DX(g_D3DD->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP));

        ASSERT_DX(g_Sampler.SetState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
        ASSERT_DX(g_D3DD->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, *((LPDWORD)(&fBias))));

        ASSERT_DX(g_D3DD->SetTexture(i, NULL));
        ASSERT_DX(g_D3DD->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, 0));
    }

    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD));

    // ASSERT_DX(g_D3DD->SetRenderState(D3DRS_TEXTUREFACTOR,		0xffffffff ));

    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHAREF, 0x08));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL));

    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW));

    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_LIGHTING, FALSE));

    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL));
    // ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZFUNC,				D3DCMP_LESS));
}
