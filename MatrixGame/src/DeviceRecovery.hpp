// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "CException.hpp"

#include <d3d9.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>

namespace Rendering {

[[noreturn]] inline void DeviceFailure(HRESULT result, std::wstring_view operation) {
    ERROR_S(std::format(L"DirectX {} failed (HRESULT=0x{:08X})", operation,
                        static_cast<uint32_t>(result)).c_str());
}

// Callbacks stay inline; recovery adds no allocation or delay to an operational frame.
template<class Release, class Reset, class Restore, class Wait>
bool RecoverDevice(HRESULT status, bool resourcesReady, Release release, Reset reset, Restore restore, Wait wait) {
    if (status == D3D_OK) {
        if (!resourcesReady) {
            restore();
        }
        return false;
    }
    if (status != D3DERR_DEVICELOST && status != D3DERR_DEVICENOTRESET) {
        DeviceFailure(status, L"TestCooperativeLevel");
    }
    if (resourcesReady) {
        release();
    }
    if (status == D3DERR_DEVICENOTRESET) {
        const HRESULT result = reset();
        if (result == D3D_OK) {
            restore();
            return false;
        }
        if (result != D3DERR_DEVICELOST) {
            DeviceFailure(result, L"Reset");
        }
    }
    wait(std::chrono::milliseconds(50));
    return true;
}

inline void CheckPresentResult(HRESULT result) {
    // Loss can occur after the frame's status check; the next tick performs recovery.
    if (result != D3D_OK && result != D3DERR_DEVICELOST) {
        DeviceFailure(result, L"Present");
    }
}

}  // namespace Rendering
