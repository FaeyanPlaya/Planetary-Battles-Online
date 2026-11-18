// MatrixGame - licensed under GPLv2 or any later version.
#include "ResourcePacks.hpp"

#include "CFile.hpp"
#include "utils.hpp"

#include <stupid_logger.hpp>
#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

namespace ResourcePacks {

namespace {

constexpr const char *description_file = "pack.txt";
constexpr const char *icon_file = "pack.png";
constexpr const wchar_t *stamp_file = L".pack-stamp";

std::wstring lower(std::wstring text) {
    for (auto &ch : text)
        if (ch >= L'A' && ch <= L'Z') ch = wchar_t(ch - L'A' + L'a');
    return text;
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return std::string(text);
}

std::wstring wide(std::string_view text) {
    try {
        return utils::to_wstring(text);
    }
    catch (...) {
        return std::wstring(text.begin(), text.end());  // not UTF-8 (an old zip's code page): bytes as they are
    }
}

std::uint32_t u16(const char *bytes) { return std::uint8_t(bytes[0]) | (std::uint8_t(bytes[1]) << 8); }
std::uint32_t u32(const char *bytes) { return u16(bytes) | (u16(bytes + 2) << 16); }

std::optional<std::string> read_file(const std::filesystem::path &file, std::size_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > limit) return std::nullopt;
    std::ifstream stream(file, std::ios::binary);
    std::string bytes(std::size_t(size), '\0');
    if (!stream.read(bytes.data(), std::streamsize(size))) return std::nullopt;
    return bytes;
}

// A zip entry's data start (after its local header), or 0.
std::uint64_t data_start(std::ifstream &stream, const ZipEntry &entry) {
    char head[30];
    stream.clear();
    stream.seekg(entry.local);
    if (!stream.read(head, 30) || u32(head) != 0x04034b50) return 0;
    return std::uint64_t(entry.local) + 30 + u16(head + 26) + u16(head + 28);
}

// A zip entry's bytes into `out`, piece by piece, its CRC checked.
template <class Sink>
bool extract(std::ifstream &stream, const ZipEntry &entry, Sink out) {
    const std::uint64_t start = data_start(stream, entry);
    if (start == 0) return false;
    stream.clear();
    stream.seekg(std::streamoff(start));
    std::vector<char> in(1 << 16), buffer(1 << 16);
    std::uint32_t crc = crc32(0, nullptr, 0);
    std::uint64_t left = entry.packed, written = 0;
    if (entry.method == 0) {
        if (entry.packed != entry.size) return false;
        while (left > 0) {
            const std::size_t n = std::size_t(std::min<std::uint64_t>(left, in.size()));
            if (!stream.read(in.data(), std::streamsize(n))) return false;
            crc = crc32(crc, reinterpret_cast<const Bytef *>(in.data()), uInt(n));
            if (!out(in.data(), n)) return false;
            left -= n;
            written += n;
        }
    }
    else {
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
        int result = Z_OK;
        while (result != Z_STREAM_END) {
            if (z.avail_in == 0) {
                if (left == 0) break;
                const std::size_t n = std::size_t(std::min<std::uint64_t>(left, in.size()));
                if (!stream.read(in.data(), std::streamsize(n))) break;
                left -= n;
                z.next_in = reinterpret_cast<Bytef *>(in.data());
                z.avail_in = uInt(n);
            }
            z.next_out = reinterpret_cast<Bytef *>(buffer.data());
            z.avail_out = uInt(buffer.size());
            result = inflate(&z, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END) break;
            const std::size_t n = buffer.size() - z.avail_out;
            if (written + n > entry.size) break;
            crc = crc32(crc, reinterpret_cast<const Bytef *>(buffer.data()), uInt(n));
            if (n && !out(buffer.data(), n)) break;
            written += n;
        }
        inflateEnd(&z);
        if (result != Z_STREAM_END) return false;
    }
    return written == entry.size && crc == entry.crc;
}

std::wstring stamp_of(const std::filesystem::path &zip) {
    std::error_code error;
    const auto size = std::filesystem::file_size(zip, error);
    const auto time = std::filesystem::last_write_time(zip, error);
    if (error) return {};
    return std::to_wstring(size) + L" " + std::to_wstring(time.time_since_epoch().count());
}

std::vector<Pack> g_installed;
bool g_scanned = false;
Choice g_started;
std::mutex g_lock;

}  // namespace

void parse(std::string_view text, Pack &pack) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    while (!text.empty()) {
        const auto end = text.find('\n');
        const std::string line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::wstring value = wide(trim(std::string_view(line).substr(eq + 1)));
        if (key == "Name") pack.name = value;
        else if (key == "NameEn") pack.name_en = value;
        else if (key == "Description") pack.description = value;
        else if (key == "DescriptionEn") pack.description_en = value;
        else if (key == "Author") pack.author = value;
    }
    if (pack.name.empty()) pack.name = pack.name_en.empty() ? pack.id : pack.name_en;
    if (pack.name_en.empty()) pack.name_en = pack.name;
    if (pack.description_en.empty()) pack.description_en = pack.description;
    if (pack.description.empty()) pack.description = pack.description_en;
}

namespace {
// A Windows device's name (CON, PRN, AUX, NUL, COM1-9, LPT1-9), with or without an extension.
bool device_name(std::wstring_view part) {
    std::wstring base = lower(std::wstring(part.substr(0, part.find(L'.'))));
    for (const wchar_t *device : {L"con", L"prn", L"aux", L"nul"})
        if (base == device) return true;
    return base.size() == 4 && (base.starts_with(L"com") || base.starts_with(L"lpt")) && base[3] >= L'1' &&
           base[3] <= L'9';
}
}  // namespace

std::wstring inside(std::wstring_view name) {
    std::wstring path(name);
    std::replace(path.begin(), path.end(), L'\\', L'/');
    while (path.starts_with(L"./")) path.erase(0, 2);
    if (path.empty() || path.starts_with(L"/")) return {};
    // Characters a Windows name cannot hold (':' a drive or a stream), controls.
    for (const wchar_t ch : path)
        if (ch < 0x20 || std::wstring_view(L"<>:\"|?*").find(ch) != std::wstring_view::npos) return {};
    std::size_t from = 0;
    while (from <= path.size()) {
        const auto slash = path.find(L'/', from);
        const std::wstring_view part = std::wstring_view(path).substr(from, slash == std::wstring::npos ? std::wstring::npos : slash - from);
        // Windows drops a name's trailing dots and spaces (".. " would be ".."): no such names, nor devices.
        if (part.empty() || part.back() == L'.' || part.back() == L' ' || device_name(part)) return {};
        if (slash == std::wstring::npos) break;
        from = slash + 1;
    }
    return path;
}

std::wstring top_of(const std::vector<std::wstring> &names) {
    std::wstring only;
    bool beside = false, several = false;
    for (const auto &name : names) {
        const std::wstring low = lower(name);
        if (low == L"pack.txt") return {};
        const auto slash = name.find(L'/');
        if (slash == std::wstring::npos) {
            beside = true;
            continue;
        }
        const std::wstring first = name.substr(0, slash + 1);
        if (only.empty()) only = first;
        else if (lower(first) != lower(only)) several = true;
    }
    for (const auto &name : names)  // pack.txt in a folder: that folder (the shallowest)
        if (lower(name).ends_with(L"/pack.txt") && std::count(name.begin(), name.end(), L'/') == 1)
            return name.substr(0, name.find(L'/') + 1);
    if (!beside && !several) return only;
    return {};
}

std::optional<std::vector<ZipEntry>> zip_entries(const std::filesystem::path &zip, std::string &error) {
    std::ifstream stream(zip, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "cannot read it";
        return std::nullopt;
    }
    const std::uint64_t size = std::uint64_t(stream.tellg());
    if (size < 22 || size > 0xffffffffull) {
        error = size < 22 ? "not a zip" : "a zip over 4 GB";
        return std::nullopt;
    }
    // The end of the central directory: its signature within the last 64 KiB (the comment) and 22 bytes.
    const std::uint64_t tail = std::min<std::uint64_t>(size, 22 + 65535);
    std::string end(std::size_t(tail), '\0');
    stream.seekg(std::streamoff(size - tail));
    if (!stream.read(end.data(), std::streamsize(tail))) {
        error = "cannot read it";
        return std::nullopt;
    }
    std::size_t at = std::string::npos;
    for (std::size_t i = end.size() - 22 + 1; i-- > 0;)
        if (u32(end.data() + i) == 0x06054b50) {
            at = i;
            break;
        }
    if (at == std::string::npos) {
        error = "not a zip";
        return std::nullopt;
    }
    const std::uint32_t count = u16(end.data() + at + 10);
    const std::uint32_t directory_size = u32(end.data() + at + 12), directory = u32(end.data() + at + 16);
    if (directory_size > size || directory > size - directory_size) {
        error = "a broken zip";
        return std::nullopt;
    }
    std::string list(directory_size, '\0');
    stream.seekg(directory);
    if (!stream.read(list.data(), std::streamsize(directory_size))) {
        error = "a broken zip";
        return std::nullopt;
    }
    std::vector<ZipEntry> entries;
    std::size_t p = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (list.size() - p < 46 || u32(list.data() + p) != 0x02014b50) {
            error = "a broken zip";
            return std::nullopt;
        }
        const char *h = list.data() + p;
        ZipEntry entry;
        const std::uint32_t flags = u16(h + 8);
        entry.method = u16(h + 10);
        entry.crc = u32(h + 16);
        entry.packed = u32(h + 20);
        entry.size = u32(h + 24);
        const std::uint32_t name_length = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        entry.local = u32(h + 42);
        if (list.size() - p - 46 < std::size_t(name_length) + extra + comment) {
            error = "a broken zip";
            return std::nullopt;
        }
        const std::string raw(h + 46, name_length);
        p += 46 + std::size_t(name_length) + extra + comment;
        if (raw.ends_with('/') || raw.ends_with('\\')) continue;  // a folder
        entry.name = inside(wide(raw));
        if (entry.name.empty()) {
            error = "a file name climbing out of the pack";
            return std::nullopt;
        }
        if ((flags & 1) || (entry.method != 0 && entry.method != 8) || entry.packed == 0xffffffffu ||
            entry.size == 0xffffffffu) {
            error = "a file the game cannot unpack (encrypted, zip64 or another method)";
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::optional<std::string> zip_file(const std::filesystem::path &zip, const ZipEntry &entry, std::uint64_t limit) {
    if (entry.size > limit) return std::nullopt;
    std::ifstream stream(zip, std::ios::binary);
    if (!stream) return std::nullopt;
    std::string bytes;
    bytes.reserve(entry.size);
    if (!extract(stream, entry, [&](const char *data, std::size_t n) {
            bytes.append(data, n);
            return true;
        }))
        return std::nullopt;
    return bytes;
}

std::vector<Pack> scan(const std::filesystem::path &packs) {
    std::vector<Pack> result;
    std::error_code failed;
    for (auto it = std::filesystem::directory_iterator(packs, failed);
         !failed && it != std::filesystem::directory_iterator(); it.increment(failed)) {
        Pack pack;
        pack.id = it->path().filename().wstring();
        pack.place = it->path();
        pack.zip = lower(it->path().extension().wstring()) == L".zip";
        if (!(it->is_directory(failed) || (pack.zip && it->is_regular_file(failed)))) continue;
        if (pack.id.find_first_of(L"=\r\n") != std::wstring::npos) continue;  // CFG\resourcepacks.txt could not name it
        std::optional<std::string> text;
        if (pack.zip) {
            std::string error;
            const auto entries = zip_entries(pack.place, error);
            if (!entries) {
                pack.problem = wide(error);
            }
            else {
                std::vector<std::wstring> names;
                for (const auto &entry : *entries) names.push_back(entry.name);
                pack.top = top_of(names);
                for (const auto &entry : *entries) {
                    const std::wstring low = lower(entry.name);
                    if (low == lower(pack.top) + L"pack.txt") text = zip_file(pack.place, entry, 64 * 1024);
                    else if (low == lower(pack.top) + L"pack.png") {
                        if (auto icon = zip_file(pack.place, entry, max_icon)) pack.icon = std::move(*icon);
                    }
                }
            }
        }
        else {
            std::vector<std::wstring> names;
            for (auto sub = std::filesystem::directory_iterator(pack.place, failed);
                 !failed && sub != std::filesystem::directory_iterator(); sub.increment(failed)) {
                std::wstring name = sub->path().filename().wstring();
                if (sub->is_directory(failed)) {
                    if (std::filesystem::exists(sub->path() / description_file, failed)) names.push_back(name + L"/pack.txt");
                    else names.push_back(name + L"/");
                }
                else names.push_back(name);
            }
            failed.clear();
            pack.top = top_of(names);
            const auto top = pack.place / pack.top;
            text = read_file(top / description_file, 64 * 1024);
            if (auto icon = read_file(top / icon_file, max_icon)) pack.icon = std::move(*icon);
        }
        parse(text ? std::string_view(*text) : std::string_view(), pack);
        result.push_back(std::move(pack));
    }
    std::ranges::sort(result, [](const Pack &a, const Pack &b) { return lower(a.id) < lower(b.id); });
    return result;
}

const std::vector<Pack> &installed() {
    std::lock_guard lock(g_lock);
    if (!g_scanned) {
        g_installed = scan(folder);
        g_scanned = true;
    }
    return g_installed;
}

void rescan() {
    std::lock_guard lock(g_lock);
    g_installed = scan(folder);
    g_scanned = true;
}

Choice parse_choice(std::string_view text) {
    Choice choice;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    while (!text.empty()) {
        const auto end = text.find('\n');
        const std::string line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.starts_with("Pack=")) continue;
        const std::wstring id = wide(trim(std::string_view(line).substr(5)));
        if (!id.empty() && std::ranges::find(choice, id) == choice.end()) choice.push_back(id);
    }
    return choice;
}

std::string choice_text(const Choice &choice) {
    std::string text = "# The resource packs on, the first over the others (the game's settings write it).\r\n";
    for (const auto &id : choice) text += "Pack=" + utils::from_wstring(id) + "\r\n";
    return text;
}

Choice load_choice() {
    const auto text = read_file(choice_file, 1024 * 1024);
    return text ? parse_choice(*text) : Choice{};
}

bool save_choice(const Choice &choice) {
    std::ofstream stream(std::filesystem::path(choice_file), std::ios::binary | std::ios::trunc);
    const std::string text = choice_text(choice);
    return stream && stream.write(text.data(), std::streamsize(text.size()));
}

bool unpack(const std::filesystem::path &zip, const std::filesystem::path &into, std::string &error) {
    const std::wstring stamp = stamp_of(zip);
    std::error_code failed;
    if (!stamp.empty()) {
        if (const auto had = read_file(into / stamp_file, 256); had && wide(*had) == stamp) return true;
    }
    const auto entries = zip_entries(zip, error);
    if (!entries) return false;
    std::filesystem::path fresh = into;
    fresh += L".new";
    std::filesystem::remove_all(fresh, failed);
    std::filesystem::create_directories(fresh, failed);
    std::uint64_t total = 0;
    for (const auto &entry : *entries) total += entry.size;
    if (total > max_unpacked) {
        error = "over 16 GB unpacked";
        std::filesystem::remove_all(fresh, failed);
        return false;
    }
    std::ifstream stream(zip, std::ios::binary);
    for (const auto &entry : *entries) {
        if (entry.size > max_file) {
            error = "a file over 512 MB";
            std::filesystem::remove_all(fresh, failed);
            return false;
        }
        const auto file = fresh / std::filesystem::path(entry.name).make_preferred();
        std::filesystem::create_directories(file.parent_path(), failed);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out || !extract(stream, entry, [&](const char *data, std::size_t n) {
                return bool(out.write(data, std::streamsize(n)));
            })) {
            error = "a broken file in the zip: " + utils::from_wstring(entry.name);
            out.close();
            std::filesystem::remove_all(fresh, failed);
            return false;
        }
    }
    {
        std::ofstream out(fresh / stamp_file, std::ios::binary | std::ios::trunc);
        const std::string text = utils::from_wstring(stamp);
        out.write(text.data(), std::streamsize(text.size()));
    }
    std::filesystem::remove_all(into, failed);
    failed.clear();
    std::filesystem::rename(fresh, into, failed);
    if (failed) {
        error = "cannot put it into " + utils::from_wstring(into.wstring());
        return false;
    }
    return true;
}

std::vector<std::wstring> pack_roots(const std::filesystem::path &top, const wchar_t *language) {
    std::vector<std::wstring> roots;
    std::error_code failed;
    const auto lang = top / L"lang" / language;
    if (std::filesystem::is_directory(lang, failed)) roots.push_back(std::filesystem::absolute(lang, failed).wstring());
    roots.push_back(std::filesystem::absolute(top, failed).wstring());
    for (auto &root : roots) {
        std::replace(root.begin(), root.end(), L'/', L'\\');
        while (root.size() > 3 && root.ends_with(L'\\')) root.pop_back();
    }
    return roots;
}

std::vector<std::wstring> start(const wchar_t *language) {
    const Choice choice = load_choice();
    const auto &packs = installed();
    std::vector<std::wstring> roots, used;
    g_started.clear();
    for (const auto &id : choice) {
        const Pack *pack = find(packs, id);
        if (!pack) {
            lgr.warning("Resource pack {} is not in the resourcepacks folder")(utils::from_wstring(id));
            continue;
        }
        if (!pack->problem.empty()) {
            lgr.error("Resource pack {}: {}")(utils::from_wstring(id), utils::from_wstring(pack->problem));
            continue;
        }
        std::filesystem::path top = pack->place / pack->top;
        if (pack->zip) {
            const auto into = std::filesystem::path(cache_folder) / pack->id;
            std::string error;
            std::error_code failed;
            std::filesystem::create_directories(cache_folder, failed);
            if (!unpack(pack->place, into, error)) {
                lgr.error("Resource pack {}: {}")(utils::from_wstring(id), error);
                continue;
            }
            top = into / pack->top;
        }
        for (auto &root : pack_roots(top, language)) roots.push_back(std::move(root));
        used.push_back(id);
        g_started.push_back(id);
    }
    Base::CFile::SetPackRoots(roots);
    if (!used.empty()) {
        std::string list;
        for (const auto &id : used) list += (list.empty() ? "" : ", ") + utils::from_wstring(id);
        lgr.info("Resource packs: {}")(list);
    }
    return used;
}

const Choice &started() { return g_started; }

}  // namespace ResourcePacks
