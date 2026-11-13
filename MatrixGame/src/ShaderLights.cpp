// MatrixGame - licensed under GPLv2 or any later version.
#include "ShaderLights.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ShaderLights {
namespace {

bool g_collecting = false;
std::vector<Light> g_lights;
std::size_t g_refused = 0;

}  // namespace

bool collecting() { return g_collecting; }

void set_collecting(bool on) {
    g_collecting = on;
    if (!on) clear();
}

void add(const Light &light) {
    if (!g_collecting) return;
    if (g_lights.size() >= max_collected) {
        ++g_refused;
        return;
    }
    g_lights.push_back(light);
}

const std::vector<Light> &collected() { return g_lights; }

std::size_t refused() { return g_refused; }

void clear() {
    g_lights.clear();
    g_refused = 0;
}

float seed(const void *who) {
    std::uint32_t h = std::uint32_t(reinterpret_cast<std::uintptr_t>(who));
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h & 0xffff) / 65536.0f;
}

std::vector<Light> pick(const std::vector<Light> &lights, float eye_x, float eye_y, float eye_z, int count,
                        const std::function<bool(const Light &)> &visible) {
    struct Scored {
        float score;
        const Light *light;
    };
    std::vector<Scored> scored;
    scored.reserve(lights.size());
    std::unordered_set<const void *> givers;
    for (const auto &light : lights) {
        if (light.radius <= 0 || !visible(light)) continue;
        if (light.who && !givers.insert(light.who).second) continue;
        const float dx = light.x - eye_x, dy = light.y - eye_y, dz = light.z - eye_z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float strength = std::max({light.r, light.g, light.b}) * light.radius;
        scored.push_back({strength / (1.0f + distance / 300.0f), &light});
    }
    // The strongest first; equal ones as they came (the same frame gives the same lights).
    std::ranges::stable_sort(scored, [](const Scored &a, const Scored &b) { return a.score > b.score; });
    std::vector<Light> result;
    std::vector<bool> taken(scored.size(), false);
    const auto take = [&](std::size_t i) {
        Light light = *scored[i].light;
        light.seed = seed(light.who);
        result.push_back(light);
        taken[i] = true;
    };
    for (std::size_t i = 0; i < scored.size() && int(result.size()) < count / 2; ++i)
        if (scored[i].light->unit) take(i);
    for (std::size_t i = 0; i < scored.size() && int(result.size()) < count; ++i)
        if (!taken[i]) take(i);
    return result;
}

}  // namespace ShaderLights
