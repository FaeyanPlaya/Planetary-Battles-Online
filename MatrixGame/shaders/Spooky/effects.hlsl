// Spooky's effects: the engine draws fire, smoke, flashes and shots by this shader in the game's place. Where an
// effect meets the ground or a wall it fades over SOFTFX of the world's units instead of being cut by a hard line.
float4 main(Effect e) {
    if (e.placed < 0.5) return e.color;  // (one drawn in the screen's coordinates: no depth to go by)
    if (e.facing < 0.5) return e.color;  // (one laid on the land, as the capture spots: it touches it all over)
    float behind = ViewDepth(SceneDepth(e.screen)) - ViewDepth(e.depth);
    return Faded(e.color, saturate(behind / SOFTFX));
}
