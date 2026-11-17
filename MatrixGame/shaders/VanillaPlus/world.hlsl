// Vanilla +'s world pass: the engine draws the land, the map's things, robots, buildings and turrets once more by this
// shader, into WorldV: how far each place moved on the screen since the previous frame (green, blue; red 1: drawn
// here). motion.hlsl reads it for the temporal filter and the motion blur.
void main(World w, out float4 moved) {
    moved = float4(1, w.motion, 1);
}
