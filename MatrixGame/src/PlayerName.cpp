// MatrixGame - licensed under GPLv2 or any later version.
#include "PlayerName.hpp"

#include "utils.hpp"

#include <filesystem>
#include <fstream>

namespace PlayerName {
namespace {

constexpr const wchar_t *file = L"CFG\\nickname.txt";

wchar_t fold(wchar_t ch) {
    if (ch >= L'A' && ch <= L'Z') return wchar_t(ch + 32);
    if (ch >= 0x0410 && ch <= 0x042F) return wchar_t(ch + 32);  // А..Я
    if (ch == 0x0401) return 0x0451;                            // Ё
    return ch;
}

}  // namespace

bool valid_char(wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'_' ||
           ch == L'-' || (ch >= 0x0410 && ch <= 0x044F) || ch == 0x0401 || ch == 0x0451;
}

bool valid(const std::wstring &name) {
    if (name.size() < min_length || name.size() > max_length) return false;
    for (wchar_t ch : name)
        if (!valid_char(ch)) return false;
    return true;
}

bool same(const std::wstring &a, const std::wstring &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (fold(a[i]) != fold(b[i])) return false;
    return true;
}

std::wstring load() {
    std::ifstream in{std::filesystem::path(file), std::ios::binary};
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    std::wstring name;
    try {
        name = utils::to_wstring(line);
    }
    catch (...) {
        return {};  // not UTF-8
    }
    return valid(name) ? name : std::wstring{};
}

bool save(const std::wstring &name) {
    if (!valid(name)) return false;
    std::ofstream out{std::filesystem::path(file), std::ios::binary | std::ios::trunc};
    out << utils::from_wstring(name) << "\r\n";
    return bool(out);
}

}  // namespace PlayerName
