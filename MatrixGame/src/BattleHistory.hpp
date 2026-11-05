// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The battle's history, opened and closed with right Ctrl pressed alone: the people's chat messages and the network
// notices (who left, came back, a desync, the delay), each with the battle time it came at, in the screen's middle. The
// game's own messages (a rally point set) are not here. Ctrl with another key or a click stays the game's own (groups,
// orders). While it is open the arrows, PgUp/PgDn and the wheel scroll it (not the camera) and Esc closes it; "• • •"
// above or below says there is more that way.
namespace BattleHistory {

constexpr std::size_t kept = 200;  // entries remembered
constexpr std::size_t shown = 20;  // lines in the panel
constexpr int wheel_lines = 3;     // lines a wheel notch scrolls

struct Entry {
    int ms = 0;  // the battle time it came at
    std::wstring text;
    std::uint32_t colour = 0xFFFFFFFF;
};

// The battle time now (a network battle sets it every step).
void set_time(int ms);
void add(const std::wstring &text, std::uint32_t colour = 0xFFFFFFFF);
const std::vector<Entry> &entries();

// "4:05", "1:02:03".
std::wstring time_text(int ms);
// The panel's hint template (the game's tooltip frame): `shown` entries ending `scroll` entries before the newest,
// oldest first, "• • •" above when older ones are hidden and below when newer ones are; `text_width` measures a line
// in Font.2Normal.
std::wstring compose(const std::vector<Entry> &entries, std::size_t scroll, bool russian,
                     const std::function<int(const std::wstring &text)> &text_width);
// The scroll kept within the entries: 0 the newest at the bottom, at most size - shown.
std::size_t clamp_scroll(std::size_t entries, long long scroll);

bool open();
std::size_t scroll();
// A key went down or up (the battle's keyboard: Ctrl comes as VK_CONTROL, which side from GetKeyState). True: the
// panel took it (the game must not see it).
bool key(std::uint8_t vk, bool down);
// The wheel turned (notches, + away from the user). True: the panel took it.
bool wheel(int notches);
// Every frame of the battle: shows, rebuilds or hides the panel.
void update();
// Tests: open the panel (a posted key does not change the key's state) scrolled `lines` up.
void test_open(int lines);
// The battle's end: the panel and the entries go.
void clear();

}  // namespace BattleHistory
