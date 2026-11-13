// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// A shader pack's voxel copy of the map (Voxels= in its shaders.txt): the whole map as a grid of small cubes, each
// holding what stands there (0 nothing, else ShaderPacks::Material: the land, an object, a building...). A pass reads
// it as a 3D texture and can trace through the world, past what the screen shows. Plain numbers only here (the tests
// link no Direct3D); ShaderRender fills it from the map and keeps the texture.
namespace ShaderVoxels {

inline constexpr int max_side = 512;    // columns along the map
inline constexpr int max_layers = 64;   // up
inline constexpr float headroom = 160;  // above the highest land: what stands on it

// The grid: voxels a side, its lowest corner in the world, a voxel's size.
struct Shape {
    int nx = 0, ny = 0, nz = 0;
    float x0 = 0, y0 = 0, z0 = 0;
    float size = 0;
    bool operator==(const Shape &) const = default;
};

// For a map of width by depth, its land between low and high: voxels of `wanted`, larger when the map would take
// more than max_side of them; a layer under the lowest land, then up to the headroom (or max_layers).
inline Shape shape(float width, float depth, float low, float high, float wanted) {
    Shape s;
    s.size = std::max({wanted, width / float(max_side), depth / float(max_side), 1.0f});
    s.nx = std::clamp(int(std::ceil(width / s.size)), 1, max_side);
    s.ny = std::clamp(int(std::ceil(depth / s.size)), 1, max_side);
    s.z0 = low - s.size;
    s.nz = std::clamp(int(std::ceil((high + headroom - s.z0) / s.size)), 8, max_layers);
    return s;
}

// The smallest power of two not under n (a 3D texture's side).
inline int texture_side(int n) {
    int side = 1;
    while (side < n) side *= 2;
    return side;
}

struct Box {
    int x0 = 0, y0 = 0, z0 = 0, x1 = -1, y1 = -1, z1 = -1;  // both ends in; x1 < x0: none
    bool empty() const { return x1 < x0; }
    void take(int x, int y, int z) {
        if (empty()) {
            x0 = x1 = x, y0 = y1 = y, z0 = z1 = z;
            return;
        }
        x0 = std::min(x0, x), x1 = std::max(x1, x);
        y0 = std::min(y0, y), y1 = std::max(y1, y);
        z0 = std::min(z0, z), z1 = std::max(z1, z);
    }
    bool meets(const Box &other) const {
        return !empty() && !other.empty() && x0 <= other.x1 && other.x0 <= x1 && y0 <= other.y1 && other.y0 <= y1 &&
               z0 <= other.z1 && other.z0 <= z1;
    }
};

class Grid {
public:
    void reset(const Shape &shape) {
        m_shape = shape;
        m_base.assign(std::size_t(shape.nx) * shape.ny * shape.nz, 0);
        m_cells = m_base;
        m_things.clear();
        m_dirty = Box{};
        if (!m_cells.empty()) m_dirty = {0, 0, 0, shape.nx - 1, shape.ny - 1, shape.nz - 1};
    }
    const Shape &shape() const { return m_shape; }
    const std::vector<std::uint8_t> &cells() const { return m_cells; }
    std::uint32_t index(int x, int y, int z) const {
        return std::uint32_t(x) + std::uint32_t(m_shape.nx) * (std::uint32_t(y) + std::uint32_t(m_shape.ny) * std::uint32_t(z));
    }
    // The voxel a place of the world is in; false outside the grid.
    bool voxel_of(float wx, float wy, float wz, int &x, int &y, int &z) const {
        if (m_shape.size <= 0) return false;
        x = int(std::floor((wx - m_shape.x0) / m_shape.size));
        y = int(std::floor((wy - m_shape.y0) / m_shape.size));
        z = int(std::floor((wz - m_shape.z0) / m_shape.size));
        return x >= 0 && y >= 0 && z >= 0 && x < m_shape.nx && y < m_shape.ny && z < m_shape.nz;
    }
    std::uint8_t at(int x, int y, int z) const { return m_cells[index(x, y, z)]; }
    // The land of a column: solid from the bottom up to its height (the voxel the height is in too).
    void fill_land(int x, int y, float height, std::uint8_t land) {
        const int top = std::min(int(std::floor((height - m_shape.z0) / m_shape.size)), m_shape.nz - 1);
        for (int z = 0; z <= top; ++z) {
            const auto i = index(x, y, z);
            m_base[i] = land;
            if (m_cells[i] == 0) m_cells[i] = land;
        }
        if (top >= 0) m_dirty.take(x, y, 0), m_dirty.take(x, y, top);
    }
    bool has(const void *who) const { return m_things.contains(who); }
    std::size_t things() const { return m_things.size(); }
    // Its bytes: the land's grid, the grid with the things, the things' voxels.
    std::size_t memory() const {
        std::size_t bytes = m_base.size() + m_cells.size();
        for (const auto &[who, thing] : m_things) bytes += thing.voxels.size() * sizeof(std::uint32_t) + sizeof(thing);
        return bytes;
    }
    // A thing's voxels (index()es; any order, repeats allowed). One with none is still known (has).
    void add(const void *who, std::uint8_t material, std::vector<std::uint32_t> voxels) {
        remove(who);
        std::ranges::sort(voxels);
        voxels.erase(std::unique(voxels.begin(), voxels.end()), voxels.end());
        Thing thing{material, {}, std::move(voxels)};
        for (const auto v : thing.voxels) {
            const Place p = place(v);
            thing.box.take(p.x, p.y, p.z);
            m_cells[v] = material;
        }
        if (!thing.box.empty()) m_dirty.take(thing.box.x0, thing.box.y0, thing.box.z0), m_dirty.take(thing.box.x1, thing.box.y1, thing.box.z1);
        m_things.emplace(who, std::move(thing));
    }
    // A thing is gone: its voxels are the land's or nothing's again, or another thing's that stands there too.
    void remove(const void *who) {
        const auto found = m_things.find(who);
        if (found == m_things.end()) return;
        const Thing gone = std::move(found->second);
        m_things.erase(found);
        for (const auto v : gone.voxels) m_cells[v] = m_base[v];
        if (gone.box.empty()) return;
        for (const auto &[other, thing] : m_things)
            if (thing.box.meets(gone.box))
                for (const auto v : thing.voxels) m_cells[v] = thing.material;
        m_dirty.take(gone.box.x0, gone.box.y0, gone.box.z0);
        m_dirty.take(gone.box.x1, gone.box.y1, gone.box.z1);
    }
    // What changed since the last call (for the texture): the box around it.
    Box take_dirty() {
        const Box box = m_dirty;
        m_dirty = Box{};
        return box;
    }

private:
    struct Place {
        int x, y, z;
    };
    Place place(std::uint32_t v) const {
        const std::uint32_t nx = std::uint32_t(m_shape.nx), ny = std::uint32_t(m_shape.ny);
        return {int(v % nx), int(v / nx % ny), int(v / nx / ny)};
    }
    struct Thing {
        std::uint8_t material = 0;
        Box box;
        std::vector<std::uint32_t> voxels;
    };
    Shape m_shape;
    std::vector<std::uint8_t> m_base;   // the land alone
    std::vector<std::uint8_t> m_cells;  // with the things
    std::unordered_map<const void *, Thing> m_things;
    Box m_dirty;
};

}  // namespace ShaderVoxels
