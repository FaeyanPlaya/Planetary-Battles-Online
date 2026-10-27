#include <Text/Font.hpp>
#include <Text/FontCache.hpp>

#include <stupid_logger.hpp>
#include <utils.hpp>

#include <Resource.h>

#include <memory>

namespace {

Text::FontCache *fontCache = nullptr;

LPD3DXFONT loadFont(IDirect3DDevice9* device, std::wstring_view name, size_t size)
{
    LPD3DXFONT font{nullptr};
    if (S_OK != D3DXCreateFontW(device, size, 0, FW_NORMAL, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, name.data(), &font))
    {
        lgr.error("Failed to load font: {}")(utils::from_wstring(name));
    }
    return font;
};

// The sized fonts of a screen drawn larger: smoothed, bold or not.
LPD3DXFONT makeFont(IDirect3DDevice9* device, std::wstring_view name, size_t size, bool bold)
{
    LPD3DXFONT font{nullptr};
    if (S_OK != D3DXCreateFontW(device, size, 0, bold ? FW_BOLD : FW_NORMAL, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                ANTIALIASED_QUALITY, DEFAULT_PITCH, name.data(), &font))
    {
        lgr.error("Failed to make font: {} {}")(utils::from_wstring(name), size);
    }
    return font;
}

void prepareRangersFont()
{
    HMODULE module = GetModuleHandle(nullptr);
    if (!module)
    {
        lgr.error("Failed to get module handle");
        return;
    }

    HRSRC fontResource = FindResource(module, MAKEINTRESOURCE(IDF_RANGERS_FONT), RT_FONT);
    if (!fontResource)
    {
        lgr.error("Failed to find font resource");
        return;
    }

    HGLOBAL loadedFont = LoadResource(module, fontResource);
    if (!loadedFont)
    {
        lgr.error("Failed to load font resource");
        return;
    }

    LPVOID lockedRes = LockResource(loadedFont);
    if (!lockedRes)
    {
        lgr.error("Failed to lock font resource");
        return;
    }

    DWORD resSize = SizeofResource(module, fontResource);
    if (!resSize)
    {
        lgr.error("Failed to get font resource size");
        return;
    }

    DWORD numFonts = 0;
    HANDLE font = AddFontMemResourceEx(lockedRes, resSize, nullptr, &numFonts);
    if (!font)
    {
        lgr.error("Failed to add font");
        return;
    }
};

} // namespace

namespace Text {

Font::Font(LPD3DXFONT font)
: m_font{font}
{
}

Font::~Font()
{
    if (m_font)
    {
        m_font->Release();
    }
}

LPD3DXFONT Font::operator -> () const
{
    return m_font;
}

Font::operator bool () const
{
    return !(m_font == nullptr);
}

size_t Font::GetHeight() const
{
    D3DXFONT_DESCW desc;
    m_font->GetDescW(&desc);
    return desc.Height;
}

size_t Font::GetSpaceWidth() const
{
    SIZE size{};
    GetTextExtentPoint32W(m_font->GetDC(), L" ", 1, &size);
    return size.cx;
}

size_t Font::CalcTextWidth(std::wstring_view text) const
{
    const DWORD format = DT_CALCRECT | DT_NOCLIP | DT_SINGLELINE;
    RECT rect{0,0,0,0};
    m_font->DrawTextW(NULL, text.data(), text.length(), &rect, format, 0);
    return rect.right;
}

Font& GetFont(IDirect3DDevice9* device, std::wstring_view font_name)
{
    // Preserve first-use initialization; notifications must not create unused fonts.
    static const auto cache = [device] {
        prepareRangersFont();
        auto result = std::make_unique<FontCache>(device, loadFont, makeFont);
        fontCache = result.get();
        return result;
    }();
    return cache->Get(font_name);
}

void OnLostDevice()
{
    if (fontCache) {
        fontCache->OnLostDevice();
    }
}

void OnResetDevice()
{
    if (fontCache) {
        fontCache->OnResetDevice();
    }
}

} // namespace Text
