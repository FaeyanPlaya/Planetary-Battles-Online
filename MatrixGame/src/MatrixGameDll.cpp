// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "MatrixGameDll.hpp"
#include "MatrixFormGame.hpp"
#include "MatrixGame.h"
#include "3g.hpp"
#include "utils.hpp"

#include <cstdio>
#include <exception>
#include <time.h>
#include <windows.h>

SMGDRobotInterface g_RobotInterface;
SMGDRangersInterface *g_RangersInterface = nullptr;
int g_ExitState = 0;

namespace {

void log_run_failure(const char *message) noexcept {
    constexpr const char *prefix = "MatrixGame DLL Run failed: ";
    OutputDebugStringA(prefix);
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    // Avoid both the logger's open file and its allocating error-path formatter.
    std::FILE *file = nullptr;
#ifdef _MSC_VER
    fopen_s(&file, "matrixgame-dll-errors.log", "ab");
#else
    file = std::fopen("matrixgame-dll-errors.log", "ab");
#endif
    if (file) {
        std::fputs(prefix, file);
        std::fputs(message, file);
        std::fputc('\n', file);
        std::fclose(file);
    }
}

int failed_run(bool end_timer) noexcept {
    ClipCursor(nullptr);
    try {
        throw;
    }
    catch (const Base::CException &exception) {
        try {
            const auto diagnostic = utils::from_wstring(exception.Info());
            log_run_failure(diagnostic.c_str());
        }
        catch (...) {
            log_run_failure("Engine exception; diagnostic unavailable");
        }
    }
    catch (const std::exception &exception) {
        log_run_failure(exception.what());
    }
    catch (...) {
        log_run_failure("Unknown C++ exception");
    }
    CGame::SafeFree(end_timer);
    g_ExitState = MATRIXGAME_RUN_ERROR;
    SETFLAG(g_Flags, GFLAG_EXITLOOP);
    return MATRIXGAME_RUN_ERROR;
}

}  // namespace

long __stdcall ExceptionHandler(PEXCEPTION_POINTERS) {
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL APIENTRY DllMain(
    [[maybe_unused]] HANDLE hModule,
    DWORD ul_reason_for_call,
    [[maybe_unused]] LPVOID lpReserved)
{
    SetUnhandledExceptionFilter(ExceptionHandler);
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

void __stdcall Init(SMGDRangersInterface *ri) {
    g_RangersInterface = ri;
}

void __stdcall Deinit() {
    g_RangersInterface = NULL;
}

int __stdcall Support() {
    // g_D3D = Direct3DCreate9(D3D_SDK_VERSION);
    g_D3D = Direct3DCreate9(31);

    if (g_D3D == NULL)
        return SUPE_DIRECTX;

    D3DCAPS9 caps;
    if (D3D_OK != g_D3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps)) {
        g_D3D->Release();
        g_D3D = NULL;
        return SUPE_DIRECTX;
    }

    if (caps.MaxSimultaneousTextures < 2)
        return SUPE_VIDEOHARDWARE;

    if (caps.MaxTextureWidth < 2048)
        return SUPE_VIDEOHARDWARE;
    if (caps.MaxTextureHeight < 2048)
        return SUPE_VIDEOHARDWARE;

    if (caps.MaxStreams == 0)
        return SUPE_VIDEODRIVER;
    g_D3D->Release();
    g_D3D = NULL;
    return SUPE_OK;
}

int __stdcall Run(HINSTANCE hinst, HWND hwnd, wchar *map, SRobotsSettings *settings, wchar *lang, wchar *txt_start,
                  wchar *txt_win, wchar *txt_loss, wchar *planet, SRobotGameState *rgs) noexcept {
    try {
        const uint32_t seed = static_cast<uint32_t>(time(nullptr));
        CGame::Init(hinst, hwnd, map, seed, settings, lang, txt_start, txt_win, txt_loss, planet);
        CFormMatrixGame formgame;
        try {
            CGame::RunGameLoop(&formgame);
            CGame::SaveResult(rgs);
            CGame::SafeFree();
            ClipCursor(nullptr);
            return FLAG(g_Flags, GFLAG_EXITLOOP) ? g_ExitState : 0;
        }
        catch (...) {
            // Keep the active stack form alive while cleanup detaches it.
            return failed_run(true);
        }
    }
    catch (...) {
        // Initialization and form construction precede the loop's timer acquisition.
        return failed_run(false);
    }
}

MATRIXGAMEDLL_API SMGDRobotInterface *__cdecl GetRobotInterface(void) {
    ZeroMemory(&g_RobotInterface, sizeof(SMGDRobotInterface));
    g_RobotInterface.m_Init = &Init;
    g_RobotInterface.m_Deinit = &Deinit;
    g_RobotInterface.m_Support = &Support;
    g_RobotInterface.m_Run = &Run;
    return &g_RobotInterface;
}
