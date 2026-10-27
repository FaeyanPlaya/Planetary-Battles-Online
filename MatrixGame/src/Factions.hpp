// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <string>

// The four sides of a planetary battle as people know them: their names and colours (CFG Side block:
// 1 yellow people, 2 red Blazer, 3 blue Keller, 4 green Terron).
namespace Factions {

inline const wchar_t *name(int side, bool russian) {
    static const wchar_t *english[] = {L"Humans", L"Blazer", L"Keller", L"Terron"};
    static const wchar_t *ru[] = {L"\u041b\u044e\u0434\u0438", L"\u0411\u043b\u0430\u0437\u0435\u0440", L"\u041a\u0435\u043b\u043b\u0435\u0440", L"\u0422\u0435\u0440\u0440\u043e\u043d"};
    return side >= 1 && side <= 4 ? (russian ? ru : english)[side - 1] : L"?";
}

// The side's colour for text on a light background (the lobby).
inline std::uint32_t colour(int side) {
    static const std::uint32_t colours[] = {0xFFE39E1F, 0xFF8E0000, 0xFF000096, 0xFF009600};
    return side >= 1 && side <= 4 ? colours[side - 1] : 0xFF000000;
}

// The side's minimap colour, for text on a dark background (battle statistics).
inline std::uint32_t bright_colour(int side) {
    static const std::uint32_t colours[] = {0xFFFFFF00, 0xFFFF4040, 0xFF5A8CFF, 0xFF40FF40};
    return side >= 1 && side <= 4 ? colours[side - 1] : 0xFFFFFFFF;
}

}  // namespace Factions
