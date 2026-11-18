// MatrixGame - SR2 Planetary battles engine
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#pragma once

// The picture a texture was read from: its name in the game's files, without the extension and the texture's
// parameters ("Matrix\Robot\Armor1" for "Matrix\Robot\Armor1?Side2"). Kept in the texture itself, so the shaders'
// world pass finds a resource pack's maps of what the game draws (_n, _s, _e, _e_s); the game itself never reads them.

#include <d3d9.h>

#include <string>

namespace TextureName {

inline const GUID &key() {
    // {5f0c3a3e-6d1b-4b7e-9a51-2c8e4f1d7b60}
    static const GUID guid = {0x5f0c3a3e, 0x6d1b, 0x4b7e, {0x9a, 0x51, 0x2c, 0x8e, 0x4f, 0x1d, 0x7b, 0x60}};
    return guid;
}

inline void set(IDirect3DResource9 *texture, const std::wstring &name) {
    if (texture && !name.empty())
        texture->SetPrivateData(key(), name.data(), DWORD(name.size() * sizeof(wchar_t)), 0);
}

inline std::wstring get(IDirect3DResource9 *texture) {
    if (!texture) return {};
    DWORD size = 0;
    if (FAILED(texture->GetPrivateData(key(), nullptr, &size)) || size == 0 || size % sizeof(wchar_t) != 0) return {};
    std::wstring name(size / sizeof(wchar_t), L'\0');
    if (FAILED(texture->GetPrivateData(key(), name.data(), &size))) return {};
    return name;
}

// A copy of a texture (the device lost and found again) keeps its picture's name.
inline void copy(IDirect3DResource9 *from, IDirect3DResource9 *to) { set(to, get(from)); }

}  // namespace TextureName
