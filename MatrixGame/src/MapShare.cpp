// MatrixGame - licensed under GPLv2 or any later version.
#include "MapShare.hpp"

#include "GameData.hpp"
#include "MapPacks.hpp"

#include "CBuf.hpp"
#include "CFile.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace MapShare {
namespace {

constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::vector<std::pair<std::wstring, std::string>> redirected;  // maps read from kept copies, with their fingerprints

std::optional<std::string> file_bytes(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

std::string fingerprint(std::string_view bytes) { return GameData::hex(GameData::hash_bytes(bytes.data(), bytes.size())); }

std::string to_base64(std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t v = std::uint32_t(std::uint8_t(bytes[i])) << 16;
        if (n > 1) v |= std::uint32_t(std::uint8_t(bytes[i + 1])) << 8;
        if (n > 2) v |= std::uint8_t(bytes[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += n > 1 ? alphabet[(v >> 6) & 63] : '=';
        out += n > 2 ? alphabet[v & 63] : '=';
    }
    return out;
}

std::optional<std::string> from_base64(std::string_view text) {
    if (text.size() % 4) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        std::uint32_t v = 0;
        int padding = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char ch = text[i + k];
            if (ch == '=' && i + 4 == text.size() && k >= 2) {
                ++padding;
                v <<= 6;
                continue;
            }
            const auto at = alphabet.find(ch);
            if (at == std::string_view::npos || padding) return std::nullopt;
            v = (v << 6) | std::uint32_t(at);
        }
        out += char(v >> 16);
        if (padding < 2) out += char((v >> 8) & 255);
        if (padding < 1) out += char(v & 255);
    }
    return out;
}

std::optional<std::string> read(const std::wstring &map) {
    if (MapPacks::of_map(MapPacks::installed(), map)) {
        auto bundle = MapPacks::bundle(map);
        if (bundle && bundle->size() > max_size) return std::nullopt;
        return bundle;
    }
    try {
        Base::CBuf data;
        data.LoadFromFile(map);
        if (std::size_t(data.Len()) > max_size) return std::nullopt;
        return std::string(data.Buff<char>(), std::size_t(data.Len()));
    }
    catch (...) {
        return std::nullopt;
    }
}

std::wstring stored(const std::string &hash) { return std::wstring(folder) + L"\\" + std::wstring(hash.begin(), hash.end()) + L".cmap"; }

bool store(const std::string &hash, std::string_view bytes) {
    if (hash.empty() || fingerprint(bytes) != hash) return false;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path file = stored(hash), part = file.native() + L".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), std::streamsize(bytes.size()));
        if (!out) return false;
    }
    std::filesystem::rename(part, file, error);
    return !error;
}

bool use(const std::wstring &map, const std::string &hash) {
    const std::wstring file = stored(hash);
    const auto bytes = file_bytes(file);
    if (!bytes || fingerprint(*bytes) != hash) return false;
    if (MapPacks::is_bundle(*bytes)) {
        // A map mod's map: its files in NetMaps\<hash>, the map at its place in the pack.
        const std::filesystem::path unpacked = std::wstring(folder) + L"\\" + std::wstring(hash.begin(), hash.end());
        std::error_code error;
        if (!std::filesystem::is_directory(unpacked, error)) {
            const std::filesystem::path part = unpacked.native() + L".part";
            std::filesystem::remove_all(part, error);
            if (!MapPacks::unpack(*bytes, part)) {
                std::filesystem::remove_all(part, error);
                return false;
            }
            std::filesystem::rename(part, unpacked, error);
            if (error) return false;
        }
        const auto in_pack = unpacked / MapPacks::maps_folder / std::filesystem::path(map).filename();
        if (!std::filesystem::is_regular_file(in_pack, error)) return false;
        Base::CFile::Redirect(map, in_pack.wstring());
        MapPacks::receive(map, unpacked.wstring());
    }
    else {
        Base::CFile::Redirect(map, file);
    }
    std::erase_if(redirected, [&](const auto &m) { return _wcsicmp(m.first.c_str(), map.c_str()) == 0; });
    redirected.emplace_back(map, hash);
    return true;
}

bool used(const std::wstring &map) { return used_hash(map).has_value(); }

std::optional<std::string> used_hash(const std::wstring &map) {
    for (const auto &[m, hash] : redirected)
        if (_wcsicmp(m.c_str(), map.c_str()) == 0) return hash;
    return std::nullopt;
}

void forget() {
    for (const auto &[map, hash] : redirected) Base::CFile::Redirect(map, {});
    redirected.clear();
    MapPacks::forget_received();
    Base::CFile::SetSearchRoots({});
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

}  // namespace MapShare
