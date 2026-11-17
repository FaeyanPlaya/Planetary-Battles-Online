// Spooky's world pass: the engine draws the land, the map's things, robots and buildings once more by this shader,
// into WorldN and WorldM. The surfaces' own normals (a model's, smooth over its rounded parts; the depth alone gives
// each triangle's) and what each thing is: for the lights in half.hlsl; and how far each place moved on the screen
// since the previous frame (WorldM's green and blue): motion.hlsl, for the temporal filter and the motion blur.
void main(World w, out float4 normal, out float4 kind) {
    normal = float4(w.normal * 0.5 + 0.5, 1);
    kind = float4(w.material / 16.0, w.motion, 1);
}
