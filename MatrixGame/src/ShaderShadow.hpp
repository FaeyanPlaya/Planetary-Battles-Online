// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <algorithm>
#include <cmath>

// A shader pack's shadow map (Shadow= in its shaders.txt): where the map light's own view of the world stands, so
// that it covers what the camera sees. Plain numbers only (the tests link no Direct3D); ShaderRender draws with them.
namespace ShaderShadow {

struct Vec {
    float x = 0, y = 0, z = 0;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec a) { return std::sqrt(dot(a, a)); }
inline Vec normalized(Vec a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec{0, 0, -1};
}

// The light's view: Direct3D's row vectors (a point times view times proj), left-handed, an orthographic projection.
struct Fit {
    float view[16]{};
    float proj[16]{};
    Vec centre;       // of what it covers
    float radius = 0;  // a square of twice this a side
    float texel = 0;   // the world's units a pixel of the map takes
    float depth = 0;   // the world's units its depth 0..1 takes
};

inline constexpr float radius_step = 64.0f;  // the covered square grows by this much at once (no creeping)

// What the camera sees: from `eye` along its four corner directions down to the ground (z = ground) or `distance`
// away, whichever is nearer. `light`: the way the light goes (down: z below 0). `size`: the map's pixels a side.
// The centre moves by whole pixels of the map: a shadow's edge does not crawl as the camera moves.
inline Fit fit(Vec eye, const Vec corners[4], float ground, float distance, Vec light, int size) {
    Vec points[5] = {eye};
    Vec sum = eye;
    for (int i = 0; i < 4; ++i) {
        const Vec dir = normalized(corners[i]);
        float reach = distance;
        if (dir.z < -1e-4f && eye.z > ground) reach = std::min(reach, (ground - eye.z) / dir.z);
        points[i + 1] = eye + dir * reach;
        sum = sum + points[i + 1];
    }
    Fit result;
    Vec centre = sum * 0.2f;
    float radius = 0;
    for (const Vec &point : points) radius = std::max(radius, length(point - centre));
    radius = std::max(radius_step, std::ceil(radius / radius_step) * radius_step);
    const Vec forward = normalized(light);
    const Vec up_hint = std::fabs(forward.z) > 0.99f ? Vec{0, 1, 0} : Vec{0, 0, 1};
    const Vec right = normalized(cross(up_hint, forward));
    const Vec up = cross(forward, right);
    const float texel = radius * 2.0f / float(std::max(size, 1));
    const float x = std::floor(dot(centre, right) / texel) * texel;
    const float y = std::floor(dot(centre, up) / texel) * texel;
    centre = right * x + up * y + forward * dot(centre, forward);
    // The light's eye stands back: what is between the light and the covered ball shades it too.
    const float back = radius * 2.0f;
    const float far_plane = back + radius;
    const Vec from = centre - forward * back;
    const float view[16] = {right.x, up.x, forward.x, 0, right.y, up.y, forward.y, 0, right.z, up.z, forward.z, 0,
                            -dot(right, from), -dot(up, from), -dot(forward, from), 1};
    const float proj[16] = {1.0f / radius, 0, 0, 0, 0, 1.0f / radius, 0, 0, 0, 0, 1.0f / far_plane, 0, 0, 0, 0, 1};
    std::copy(std::begin(view), std::end(view), result.view);
    std::copy(std::begin(proj), std::end(proj), result.proj);
    result.centre = centre;
    result.radius = radius;
    result.texel = texel;
    result.depth = far_plane;
    return result;
}

// A point of the world in the map: x, y -1..1 (y up), z 0 at the light's eye to 1.
inline Vec clip(const Fit &fit, Vec p) {
    const float *v = fit.view;
    const Vec in_view{p.x * v[0] + p.y * v[4] + p.z * v[8] + v[12], p.x * v[1] + p.y * v[5] + p.z * v[9] + v[13],
                      p.x * v[2] + p.y * v[6] + p.z * v[10] + v[14]};
    return {in_view.x * fit.proj[0], in_view.y * fit.proj[5], in_view.z * fit.proj[10]};
}

// Whether a ball (a thing's: its centre, its radius) reaches what the map covers: one outside it is drawn there for
// nothing. A fit not made yet covers everything.
inline bool covers(const Fit &fit, Vec centre, float radius) {
    if (!(fit.radius > 0 && fit.depth > 0)) return true;
    const Vec p = clip(fit, centre);
    const float side = 1.0f + radius / fit.radius, depth = radius / fit.depth;
    return std::fabs(p.x) <= side && std::fabs(p.y) <= side && p.z >= -depth && p.z <= 1.0f + depth;
}

}  // namespace ShaderShadow
