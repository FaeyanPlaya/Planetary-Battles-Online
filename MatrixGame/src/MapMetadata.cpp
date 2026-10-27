// MatrixGame - licensed under GPLv2 or any later version.
#include "MapMetadata.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace MapMetadata {
namespace {

constexpr size_t maximum_bytes = 16 * 1024 * 1024;
constexpr size_t maximum_nodes = 100000;
constexpr size_t maximum_text = 65536;

[[noreturn]] void invalid() { throw std::runtime_error("Invalid or unsupported language DAT"); }

uint32_t u32(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) invalid();
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

uint32_t crc(std::span<const uint8_t> bytes) {
    return static_cast<uint32_t>(crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}

struct Node {
    std::wstring name, value;
    std::vector<Node> children;
    bool block = false;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes(bytes) {}

    Node node(size_t depth = 0, bool root = false) {
        if (depth >= 64 || ++nodes > maximum_nodes) invalid();
        Node result;
        const uint8_t type = root ? 2 : take(1)[0];
        if (type != 1 && type != 2) invalid();
        if (!root) result.name = text();
        result.block = type == 2;
        if (!result.block) {
            result.value = text();
            return result;
        }
        const uint8_t sorted = take(1)[0];
        if (sorted > 1) invalid();
        const uint32_t count = u32(take(4), 0);
        if (count > maximum_nodes - nodes) invalid();
        for (uint32_t i = 0; i < count; ++i) {
            if (sorted) take(8);  // Stored sort indexes; stream order preserves duplicate paragraphs.
            result.children.push_back(node(depth + 1));
        }
        return result;
    }

    bool finished() const { return offset == bytes.size(); }

private:
    std::span<const uint8_t> bytes;
    size_t offset = 0, nodes = 0;

    std::span<const uint8_t> take(size_t size) {
        if (size > bytes.size() - offset) invalid();
        auto result = bytes.subspan(offset, size);
        offset += size;
        return result;
    }

    std::wstring text() {
        std::wstring result;
        bool surrogate = false;
        for (;;) {
            const auto pair = take(2);
            const uint16_t ch = uint16_t(pair[0]) | (uint16_t(pair[1]) << 8);
            if (surrogate) {
                if (ch < 0xdc00 || ch > 0xdfff) invalid();
                surrogate = false;
            }
            else if (ch >= 0xd800 && ch <= 0xdbff) surrogate = true;
            else if (ch >= 0xdc00 && ch <= 0xdfff) invalid();
            if (!ch) return result;
            if (result.size() >= maximum_text) invalid();
            result += static_cast<wchar_t>(ch);
        }
    }
};

const Node *child(const Node &node, const wchar_t *name) {
    const Node *result = nullptr;
    for (const auto &item : node.children) {
        if (item.name != name) continue;
        if (result) invalid();  // Singleton metadata fields must be unambiguous.
        result = &item;
    }
    return result;
}

std::wstring value(const Node &node, const wchar_t *name) {
    const auto *item = child(node, name);
    if (item && item->block) invalid();
    return item ? item->value : L"";
}

std::wstring substitute(std::wstring_view text, const std::array<std::wstring, 3> &values) {
    constexpr std::wstring_view tokens[] = {L"<Player>", L"<Star>", L"<Planet>"};
    std::wstring result;
    for (size_t at = 0; at < text.size();) {
        std::wstring_view piece = text.substr(at, 1);
        size_t consumed = 1;
        for (size_t i = 0; i < values.size(); ++i) {
            if (text.substr(at).starts_with(tokens[i])) { piece = values[i]; consumed = tokens[i].size(); break; }
        }
        if (piece.size() > maximum_text - result.size()) invalid();
        result.append(piece);
        at += consumed;
    }
    return result;
}

std::wstring single_line(std::wstring text) {
    text = plain_text(std::move(text));
    std::replace(text.begin(), text.end(), L'\n', L' ');
    return text;
}

std::wstring filename(std::wstring name) {
    if (name.size() < 6 || name.size() > 255) return {};
    for (auto &ch : name) {
        if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') ||
              std::wstring_view(L" _-.()+!#&,'~").find(ch) != std::wstring_view::npos)) return {};
    }
    if (!name.ends_with(L".cmap")) return {};
    return name;
}

Catalog catalog(const Node &root) {
    const Node *maps = child(root, L"RobotsMap");
    if (!maps || !maps->block) invalid();
    const Node *form = child(root, L"FormLoadRobot");
    if (form && !form->block) invalid();
    std::array<std::wstring, 3> replacements;
    size_t token_index = 0;
    for (const auto *token : {L"PPlayer", L"PStar", L"PPlanet"})
        replacements[token_index++] = form ? plain_text(value(*form, token)) : L"";
    Catalog result;
    for (const auto &map : maps->children) {
        if (!map.block) invalid();
        const auto file = filename(value(map, L"Map"));
        if (file.empty()) continue;
        Entry entry;
        entry.name = single_line(value(map, L"Name"));
        entry.sides = single_line(value(map, L"Side"));
        const auto group = value(map, L"Group"), length = value(map, L"Length");
        if (group.size() == 1 && group[0] >= L'0' && group[0] <= L'2') entry.group = group[0] - L'0';
        if (length.size() == 1 && length[0] >= L'0' && length[0] <= L'4') entry.length = length[0] - L'0';
        for (const auto &paragraph : map.children) {
            if (paragraph.name != L"RobotsStart") continue;
            if (paragraph.block) invalid();
            if (!entry.description.empty()) entry.description += L"\n\n";
            entry.description += paragraph.value;
            if (entry.description.size() > maximum_text) invalid();
        }
        entry.description = plain_text(substitute(entry.description, replacements));
        if (!result.emplace(file, std::move(entry)).second) invalid();
    }
    return result;
}

}  // namespace

std::wstring plain_text(std::wstring text) {
    std::wstring result;
    size_t tag_start = std::wstring::npos;
    for (auto ch : text) {
        if (ch == L'<') { if (tag_start == std::wstring::npos) tag_start = result.size(); ch = L'('; }
        else if (ch == L'>' && tag_start != std::wstring::npos) {
            result.resize(tag_start); tag_start = std::wstring::npos; continue;
        }
        if (ch == L'\n') tag_start = std::wstring::npos; // Do not discard later paragraphs for an unclosed tag.
        if (ch == L'|') ch = L'/';
        if (ch == L'[') ch = L'(';
        if (ch == L']') ch = L')';
        if (ch == L'\r') continue;
        if (ch < L' ' && ch != L'\n') ch = L' ';
        result += ch;
    }
    return result;
}

Catalog decode(std::span<const uint8_t> bytes) {
    if (bytes.size() < 20 || bytes.size() > maximum_bytes) invalid();
    const auto body = bytes.subspan(8);
    const uint32_t first = static_cast<uint32_t>(body.size()) ^ 0xc83fcbf3u ^ 0x7db6c99du;
    const uint32_t signature = crc(body) ^ 0x7db6c99du;
    std::array<uint8_t, 4> encoded{};
    for (size_t i = 0; i < 4; ++i) encoded[i] = static_cast<uint8_t>(signature >> (8 * i));
    const uint32_t second = static_cast<uint32_t>(crc32(crc(encoded), body.data(), static_cast<uInt>(body.size()))) ^ 0xc83fcbf3u;
    if (u32(bytes, 0) == first && u32(bytes, 4) == second) bytes = body;
    // Language DAT uses the sorted Main tree, in HD or Reload form. Cache DAT is a different tree.
    for (uint32_t key : {static_cast<uint32_t>(-1310144887), uint32_t(1050086386)}) {
        const uint32_t seed_bits = u32(bytes, 4) ^ key;
        int64_t state = seed_bits <= 0x7fffffffu ? int64_t(seed_bits) : int64_t(seed_bits) - 0x100000000ll;
        std::vector<uint8_t> clear;
        clear.reserve(bytes.size() - 8);
        for (auto byte : bytes.subspan(8)) {
            // Python/host format uses floor division for the first, possibly negative seed.
            int64_t high = state / 127773, low = state % 127773;
            if (low < 0) { --high; low += 127773; }
            state = low * 16807 - high * 2836;
            if (state < 1) state += 2147483647;
            clear.push_back(byte ^ static_cast<uint8_t>(state - 1));
        }
        if (clear.size() < 8 || !std::equal(clear.begin(), clear.begin() + 4, "ZL01") || crc(clear) != u32(bytes, 0)) continue;
        const uint32_t size = u32(clear, 4);
        if (!size || size > maximum_bytes) invalid();
        std::vector<uint8_t> decoded(size);
        uLongf output_size = size;
        uLong input_size = static_cast<uLong>(clear.size() - 8);
        if (uncompress2(decoded.data(), &output_size, clear.data() + 8, &input_size) != Z_OK ||
            output_size != size || input_size != clear.size() - 8) invalid();
        Reader reader(decoded);
        auto root = reader.node(0, true);
        if (!reader.finished()) invalid();
        return catalog(root);
    }
    invalid();
}

Catalog load(std::string &error, const std::filesystem::path &path) {
    error.clear();
    try {
        if (!std::filesystem::exists(path)) return {};
        const auto size = std::filesystem::file_size(path);
        if (size > maximum_bytes) invalid();
        std::ifstream input(path, std::ios::binary);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) invalid();
        return decode(bytes);
    }
    catch (const std::exception &failure) { error = failure.what(); return {}; }
}

}  // namespace MapMetadata
