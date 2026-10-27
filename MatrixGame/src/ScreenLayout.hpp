// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// Keeps the battle interface consistent with the rendering size (g_ScreenX/g_ScreenY), which the
// standalone display controller can change during a session.
namespace ScreenLayout {

struct Position {
    int x, y;
    bool operator==(const Position &) const = default;
};

// Top-left corner of a dialog centred on the screen and raised by 9% of the screen height.
Position centred_dialog(int screen_width, int screen_height, int width, int height);

// Interface layouts are authored for 1024x768. A nonzero layout coordinate keeps its distance from
// the right or bottom edge; zero stays at the left or top edge.
constexpr int layout_width = 1024;
constexpr int layout_height = 768;
int anchor(int layout_position, int layout_extent, int screen_extent);

// Where a dialog at current moves when the screen changes size: centred dialogs stay centred,
// dialogs shown elsewhere keep their position.
Position follow_screen(Position current, int width, int height, int old_screen_width, int old_screen_height,
                       int new_screen_width, int new_screen_height);

// Records the size the interface was laid out for. Call after the battle interface is loaded.
void laid_out(int width, int height);

// Re-lays out the camera, edge-anchored panels and open dialogs when the rendering size differs
// from the last layout. Returns true when it did.
bool update();

}  // namespace ScreenLayout
