// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// The battle's lights for a shader pack (docs/SHADERS.md, Lights): fires, explosions, shots, flashes. While a pack
// draws, the effects add theirs as the frame is made ready (CMatrixEffect::AddShaderLights); the pack's drawing takes
// the strongest the camera may see. Drawing only: nothing of the battle reads them.
namespace ShaderLights {

enum class Kind { Glow = 0, Fire = 1, Flash = 2, Shot = 3 };

struct Light {
    float x = 0, y = 0, z = 0;  // in the world (z up)
    float radius = 0;           // where its light ends
    float r = 0, g = 0, b = 0;  // its colour at the middle (0..1, more for a very bright one)
    Kind kind = Kind::Glow;
    float seed = 0;             // 0..1, the same while it lives (a pack's flicker)
    const void *who = nullptr;  // what gives it (a light added twice is taken once)
    bool unit = false;          // a robot's or flyer's own (its jets): taken before the battle's others
};

inline constexpr int max_lights = 32;            // what a pass gets
// What a frame keeps: the whole map's effects come first (their lights as the frame is made ready), the robots' and
// flyers' jets as they are drawn; a long battle's fires all over the map filled 512, and the jets were left out.
inline constexpr std::size_t max_collected = 4096;

// Whether a pack draws (otherwise add does nothing).
bool collecting();
void set_collecting(bool on);

void add(const Light &light);
template<class V>
void add(const V &pos, float radius, float r, float g, float b, Kind kind, const void *who, bool unit = false) {
    if (!collecting() || radius <= 0 || (r <= 0 && g <= 0 && b <= 0)) return;
    add(Light{pos.x, pos.y, pos.z, radius, r, g, b, kind, 0, who, unit});
}
// An effect's colour (0x..RRGGBB: as the game lights the ground, its alpha left out).
template<class V>
void add_argb(const V &pos, float radius, std::uint32_t argb, Kind kind, const void *who, float strength = 1) {
    const float k = strength / 255.0f;
    add(pos, radius, float((argb >> 16) & 0xff) * k, float((argb >> 8) & 0xff) * k, float(argb & 0xff) * k, kind,
        who);
}

// This frame's, as added; clear empties them. refused: those added past max_collected since the last clear.
const std::vector<Light> &collected();
std::size_t refused();
void clear();

// The 0..1 seed of what gives a light.
float seed(const void *who);
// At most `count` of `lights`: those `visible` says the camera may see, each `who` once, the strongest by their
// distance to the eye first; their seeds set. The units' own first, up to half of `count`: a battle can show
// hundreds of lights, and a robot's small jets went out among the fires and blasts near the camera.
std::vector<Light> pick(const std::vector<Light> &lights, float eye_x, float eye_y, float eye_z, int count,
                        const std::function<bool(const Light &)> &visible);

}  // namespace ShaderLights
