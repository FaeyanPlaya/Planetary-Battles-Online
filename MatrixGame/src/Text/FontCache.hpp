// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "Font.hpp"

#include <map>
#include <string>

namespace Text {

// Owns the fixed UI font set for one device; the loader also supports asset-free tests.
// With a maker, sized fonts are also made on first use: Font.Heavy<px> (the menu's Verdana, drawn heavier: see
// heavy_stroke) and Font.Title<px> (the Rangers face), for a screen drawn larger (Font.Heavy23).
class FontCache {
public:
    using Loader = LPD3DXFONT (*)(IDirect3DDevice9 *, std::wstring_view, size_t);
    using Maker = LPD3DXFONT (*)(IDirect3DDevice9 *, std::wstring_view face, size_t size);

    FontCache(IDirect3DDevice9 *device, Loader loader, Maker maker = nullptr);
    FontCache(const FontCache &) = delete;
    FontCache &operator=(const FontCache &) = delete;

    Font &Get(std::wstring_view name);
    void OnLostDevice();
    void OnResetDevice();

private:
    std::map<std::wstring_view, Font> m_Fonts;
    std::map<std::wstring, Font, std::less<>> m_Made;
    IDirect3DDevice9 *m_Device = nullptr;
    Maker m_Maker = nullptr;
};

// How far a font's text is smeared to the right when drawn, in pixels (a part of a pixel is drawn fainter); 0: not.
// Verdana has only Regular and Bold, whose letters are other shapes: the screens before a battle keep the regular
// letters and make them heavier this way, the same at any scale.
float heavy_stroke(std::wstring_view font_name);

}  // namespace Text
