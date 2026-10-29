// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <optional>
#include <string>
#include <string_view>

// A lobby's host gives its map file to a client that does not have it, or has another file under the same
// name. The client keeps the copy in NetMaps beside the EXE, named by its fingerprint, and reads it under the
// host's name (CFile::Redirect) until the lobby or battle is over; its own map files are never touched. A map mod's
// map goes as a bundle with the mod's files it takes (MapPacks::bundle), unpacked into NetMaps\<hash>.
namespace MapShare {

inline constexpr const wchar_t *folder = L"NetMaps";
inline constexpr std::size_t max_size = 256u << 20;  // larger files are not sent (a map mod's bundle is the largest)
inline constexpr std::size_t chunk_size = 48u << 10;  // bytes in one line (64 KiB of base64)

// GameData::hex(GameData::hash_bytes(...)) of the bytes, as Lobby::State::map_hash holds it.
std::string fingerprint(std::string_view bytes);

std::string to_base64(std::string_view bytes);
// nullopt on a character outside the alphabet or a broken length.
std::optional<std::string> from_base64(std::string_view text);

// The map file's bytes, loose or packaged (a map mod's map: its bundle); nullopt when it cannot be read or is larger
// than max_size.
std::optional<std::string> read(const std::wstring &map);

// NetMaps\<hash>.cmap.
std::wstring stored(const std::string &hash);
// Keeps a received copy; false when the bytes do not have this fingerprint or cannot be written.
bool store(const std::string &hash, std::string_view bytes);
// Reads `map` from the kept copy with this fingerprint from now on (a bundle: unpacked once, its files found first
// for that map); false when there is none.
bool use(const std::wstring &map, const std::string &hash);
// Whether `map` is read from a kept copy (use).
bool used(const std::wstring &map);
// The fingerprint of the kept copy `map` is read from, or nullopt.
std::optional<std::string> used_hash(const std::wstring &map);
// Back to this PC's own files: no copies are read, and the kept ones are deleted.
void forget();

}  // namespace MapShare
