// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// Шутер (the shooter mod, Shooter, with Manual control+): the engine's side of it, what this person sees and does (the
// battle's side is the mod's: ModHooks' shooter_turn). In a robot or turret driven the view looks from it (the first
// person camera, held) and the mouse turns it: the game's cursor stays in the screen's middle, where the aim is (drawn
// by the game there, the Windows cursor held in the window and hidden); a ring shows where the robot's hull (its
// weapons fire along it) or the turret's head looks now. Its way in
// (0 out, 1 in, as the camera's way into the robot) goes to the shader packs as MOD_SHOOTER.
class CMatrixRobotAI;
struct D3DXVECTOR3;

namespace Shooter {

// The battle's Shooter with Manual control+ (its sideways), and the mod's code.
bool on();
// This person drives a robot or turret in it now.
bool driving();
// The mouse is the view's now: driving, the battle going on (no pause, no dialog), the window in front.
bool captured();
// The mouse moved (x, y in the game's screen): the view turns by it and the cursor goes back to the middle. True: the
// move was the view's.
bool mouse_move(int x, int y);
// Each frame: the cursor held in the window and in its middle while captured (let go otherwise); the way in for the
// shader packs.
void frame(int ms);
// The window lost the focus (or the battle ends): the cursor let go.
void release();
// The mark where the hull or the head looks now (drawn over the battle and the interface, under the cursor).
void draw_marker();
// The way in now: 0 out, 1 in (smoothed).
float way_in();
// Where a driven robot's weapons fire, and its ring is: along its hull as far as the aim and as high (the aim itself
// once the hull is on it). The battle's: the same on every peer.
D3DXVECTOR3 hull_aim(const CMatrixRobotAI &robot, const D3DXVECTOR3 &aim);

}  // namespace Shooter
