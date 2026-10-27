// MatrixGame - licensed under GPLv2 or any later version.
#include "FontCache.hpp"

#include "utils.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>

namespace Text {
namespace {
void check_notification(HRESULT result, std::wstring_view name, const char *operation) {
    if (FAILED(result)) {
        throw std::runtime_error(std::format("Font {} failed for {} (HRESULT=0x{:08X})",
                                            operation, utils::from_wstring(name), static_cast<uint32_t>(result)));
    }
}
}

FontCache::FontCache(IDirect3DDevice9 *device, Loader loader, Maker maker) : m_Device(device), m_Maker(maker) {
    m_Fonts.emplace(L"Font.1Normal", loader(device, L"Verdana", 13));
    m_Fonts.emplace(L"Font.2Mini", loader(device, L"Verdana", 12));
    m_Fonts.emplace(L"Font.2Small", loader(device, L"Verdana", 13));
    m_Fonts.emplace(L"Font.2Normal", loader(device, L"Verdana", 14));
    m_Fonts.emplace(L"Font.2Ranger", loader(device, L"Rangers", 10));
}

Font &FontCache::Get(std::wstring_view name) {
    if (const auto found = m_Fonts.find(name); found != m_Fonts.end()) return found->second;
    if (const auto made = m_Made.find(name); made != m_Made.end()) return made->second;
    const auto sized = [&](std::wstring_view prefix) {
        if (!m_Maker || !name.starts_with(prefix) || name.size() == prefix.size() || name.size() > prefix.size() + 3) return 0;
        int px = 0;
        for (wchar_t ch : name.substr(prefix.size())) {
            if (ch < L'0' || ch > L'9') return 0;
            px = px * 10 + (ch - L'0');
        }
        return px >= 6 && px <= 96 ? px : 0;
    };
    if (const int px = sized(L"Font.Bold"))
        return m_Made.try_emplace(std::wstring(name), m_Maker(m_Device, L"Verdana", size_t(px), true)).first->second;
    if (const int px = sized(L"Font.Title"))
        return m_Made.try_emplace(std::wstring(name), m_Maker(m_Device, L"Rangers", size_t(px), false)).first->second;
    throw std::runtime_error("Unknown font: " + utils::from_wstring(name));
}

void FontCache::OnLostDevice() {
    for (auto &[name, font] : m_Fonts) {
        if (font) {
            check_notification(font->OnLostDevice(), name, "OnLostDevice");
        }
    }
    for (auto &[name, font] : m_Made)
        if (font) check_notification(font->OnLostDevice(), name, "OnLostDevice");
}

void FontCache::OnResetDevice() {
    for (auto &[name, font] : m_Fonts) {
        if (font) {
            check_notification(font->OnResetDevice(), name, "OnResetDevice");
        }
    }
    for (auto &[name, font] : m_Made)
        if (font) check_notification(font->OnResetDevice(), name, "OnResetDevice");
}

}  // namespace Text
