// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

// The window of a shared battle that cannot go on: whom everyone waits for and how long, when that
// person will be removed, and for the host a Remove button for each of them. Also the window of a
// person the host has removed (with Exit). It looks like the game's own confirmation dialogs (hint
// border 2, black text).
namespace BattleWait {

constexpr int row_height = 22;
constexpr int padding_x = 64;  // the text area inside the frame (clear of its question mark icon)
constexpr int padding_top = 22;
constexpr int padding_bottom = 24;
constexpr int min_width = 340;  // the frame's corners need about 290

struct Person {
    int side = 0;
    int peer = -1;       // the host's connection to them (for Remove); -1: not the host's to remove
    std::wstring name;   // empty: none, the faction alone
    int waited_ms = 0;
    bool host = false;     // the host itself does not answer (a client's window)
    bool loading = false;  // before the start: still loading the battle
};

struct Window {
    std::vector<Person> people;
    int drop_in_ms = -1;      // when the longest waited for is removed; -1: no time limit (loading)
    bool can_remove = false;  // the host: a Remove button per person
    // This person was removed: 1 for not answering (Rejoin, Exit), 2 by the host (Exit).
    int dropped = 0;
    bool dropped_ai = true;  // BattleRules::dropped_ai: who plays a side while its person is away
    bool host_lost = false;   // the connection to the host was lost: Rejoin, Play on alone
    int catching_up = -1;     // back in the battle, replaying it: how far, in percent; no buttons
};

// "Rover (Келлер)" or the faction alone.
std::wstring who(const Person &person, bool russian);
// One person's line: "Rover (Келлер) не отвечает: 12 с", "Хост Rover (Люди) не отвечает: 12 с",
// "Rover (Келлер) загружает бой".
std::wstring person_line(const Person &person, bool russian);

// The window's hint template (border 2) and one template per button (no border), laid out for `width`
// measuring a text's pixel width in a font. Buttons: Remove per person for the host, else Exit for a
// removed person; `hover` is the button under the mouse (-1 none). Button i is drawn at the window's
// (padding_x, buttons_y + i * row_height), width - 2 * padding_x wide and row_height high.
struct Templates {
    std::wstring window;
    std::vector<std::wstring> buttons;
    int width = 0;
    int buttons_y = 0;
};
Templates compose(const Window &window, bool russian, int hover,
                  const std::function<int(const std::wstring &text, const std::wstring &font)> &text_width);

// Shows the window (built again only when its text changed), centred like the game's dialogs.
void show(const Window &window, bool russian);
void hide();
bool shown();
// The mouse is at (x, y) on the screen: the button under it lights up.
void hover(int x, int y);
// The button at (x, y): the person's Person::peer for Remove; for a removed person or a lost host the
// button's index (0 the first); -1: none.
int button_at(int x, int y);
// Screen rectangles of the shown buttons, for tests: x, y, width, height each.
std::vector<std::array<int, 4>> button_rects();
// Releases the hints (before the battle's interface goes).
void clear();

}  // namespace BattleWait
