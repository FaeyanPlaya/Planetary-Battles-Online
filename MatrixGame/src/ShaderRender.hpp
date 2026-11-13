// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <string>

struct IDirect3DTexture9;
struct _D3DMATRIX;
class CMatrixMapStatic;

// The chosen shader pack (ShaderPacks) over the battle's picture: the frame's 3D is drawn into a texture of its own
// with a depth the passes can read, then the pack's passes draw it onto the screen, and the interface comes on top.
// Drawing only: nothing here is read by a battle.
namespace ShaderRender {

// Before the frame is cleared: with a pack on and ready, the 3D goes into the pack's scene texture (and the effects
// add their lights, ShaderLights).
void begin_frame();
// A pack's shadow map (its Shadow= line): before the picture's 3D, true when this frame has one. Its depth is then
// the target and the device's view and projection are the map light's over what the camera sees: the caller draws
// what casts shadows (the objects, the land) and calls shadow_end, then sets the camera's view and projection.
bool shadow_begin();
void shadow_end();
// Whether a thing (its ball: the centre, the radius) reaches what this frame's shadow map covers.
bool shadow_keeps(float x, float y, float z, float radius);
// A pack's world pass (its World= line): before the picture's 3D, true when this frame has one. Its targets are then
// the device's and its shader draws what the caller draws next (the objects, the land: g_WorldDrawHook sets each
// draw up); world_material before a thing says what it is (ShaderPacks::Material) and which (its address: where
// its parts were a frame ago is kept by it; null: the land, which does not move). Then world_end.
bool world_begin();
void world_material(int material, const void *who);
// Whether a thing of the map's own (not one that fights) is big enough on the screen for the world pass to draw it.
bool world_keeps(float x, float y, float z, float radius);
void world_end();
// A pack's water (its Water= line): instead of the game's water passes, true when the pack draws it this frame. The
// caller sets the water's vertices and draws its parts, water_part before each (the map's mask of where the water is
// there, null: all of it, outside the map; the part's world matrix), then water_end.
bool water_begin(IDirect3DTexture9 *texture, IDirect3DTexture9 *reflection);
void water_part(IDirect3DTexture9 *mask, const _D3DMATRIX &world);
void water_end();
// A pack's effects (its Effects= line): between these the effects' draws go through its shader (g_WorldDrawHook
// sets each up from the game's texture stages; one the code cannot do stays the game's).
void effects_begin();
void effects_end();
// The 3D without the effects is drawn (CMatrixMap::Draw, before DrawEffects): kept for the passes' SolidTex, its
// alpha 0 where the game's volumetric shadows fell (from the stencil they were drawn with).
void before_effects();
// Whether SolidTex was taken this frame: then the interface's marks on the ground (selection rings, paths, the
// resources' figures) are drawn after the effects into the picture and, between solid_target and scene_target, into
// SolidTex too, so the pack does not take them for glow.
bool solid_taken();
bool solid_target();
void scene_target();
// After the battle's 3D (CMatrixMap::Draw, before the interface): the passes onto the screen, which becomes the
// target again. Nothing when begin_frame did not take the frame.
void finish_scene();
// A thing of the map is being deleted: a pack's voxel copy (its Voxels= line) lets go of it.
void voxels_forget(const CMatrixMapStatic *object);
// Measuring (PBO_SHADER_TIMING=1 in the environment: the lab's runs): the video card's time for each part of a
// frame the pack draws, averaged over 120 frames into test.log, with the frame's own time; the voxel copy's build
// and the textures' memory. Nothing without it.
enum class Part { Shadow, World, Water, Effects, Passes, Frame, Count };
void time_begin(Part part);
void time_end(Part part);
// Lost device: the default pool's textures go (made again on the next frame).
void release();
// The battle is over: everything goes.
void shutdown();
// Whether the game draws its own distance fog: not while a pack that turns it off draws (Game.Fog=Off). The pack's
// other game settings (shadows) go into g_Config as each frame begins, the person's back once it stops.
bool game_fog();
// Why the chosen pack does not draw (its code's errors, a texture, the video card), or empty.
const std::wstring &problem();

}  // namespace ShaderRender
