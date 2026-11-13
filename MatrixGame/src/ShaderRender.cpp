// MatrixGame - licensed under GPLv2 or any later version.
#include "ShaderRender.hpp"

#include "ShaderPacks.hpp"
#include "ShaderLights.hpp"
#include "ShaderShadow.hpp"
#include "ShaderVoxels.hpp"
#include "MatrixMapGroup.hpp"
#include "MatrixMapStatic.hpp"
#include "MatrixMap.hpp"
#include "MatrixConfig.hpp"
#include "StandaloneRuntime.hpp"
#include "3g.hpp"
#include "utils.hpp"

#include <d3dx9.h>
#include <stupid_logger.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <future>
#include <memory>
#include <unordered_set>
#include <deque>
#include <map>
#include <optional>
#include <cstring>
#include <vector>

namespace ShaderRender {
namespace {

template<class T>
void drop(T *&object) {
    if (object) object->Release();
    object = nullptr;
}

constexpr D3DFORMAT intz = D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'));
constexpr D3DFORMAT null_format = D3DFORMAT(MAKEFOURCC('N', 'U', 'L', 'L'));  // a target that keeps nothing
constexpr int picture_sampler = 3;  // a pass's targets and textures from here on (ShaderPacks::Inputs)
constexpr int solid_sampler = 15;
constexpr int lights_register = 32;  // LightCount, then LightPos, LightColor, LightInfo: max_lights each

// The full-screen quad's vertex shader: the pixel centres on the texels' (Direct3D 9's half pixel).
constexpr const char *quad_vs =
    "float4 HalfPixel : register(c0);\n"
    "struct Out { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "Out main(float4 pos : POSITION, float2 uv : TEXCOORD0) {\n"
    "    Out o;\n"
    "    o.pos = float4(pos.x - HalfPixel.x, pos.y + HalfPixel.y, 0, 1);\n"
    "    o.uv = uv;\n"
    "    return o;\n"
    "}\n";

// The water's vertices (MATRIX_WATER_VERTEX_FORMAT) as the game's fixed pipeline treats them: its light on each
// (its ambient, the water's colour, and a directional light by the normal as the game leaves it: not of unit length
// unless the game has normals normalized, LightDir.w the factor; 0: normalized), the reflection's place by the
// normal in the camera's space, the fog by the depth (the game's linear fog of the table).
constexpr const char *water_vs =
    "row_major float4x4 WorldM : register(c0);\n"
    "row_major float4x4 ViewProjM : register(c4);\n"
    "row_major float4x4 ViewM : register(c8);\n"
    "float4 LightDir : register(c12);\n"
    "float4 LightDiffuse : register(c13);\n"
    "float4 LightAmbient : register(c14);\n"
    "float4 Fog : register(c15);\n"
    "struct In { float4 pos : POSITION; float3 normal : NORMAL; float2 mask : TEXCOORD0; float2 uv : TEXCOORD1; };\n"
    "struct Out {\n"
    "    float4 hpos : POSITION;\n"
    "    float3 pos : TEXCOORD0;\n"
    "    float3 normal : TEXCOORD1;\n"
    "    float2 mask : TEXCOORD2;\n"
    "    float2 uv : TEXCOORD3;\n"
    "    float2 refl : TEXCOORD4;\n"
    "    float4 light : TEXCOORD5;\n"
    "};\n"
    "Out main(In i) {\n"
    "    Out o;\n"
    "    float4 p = mul(float4(i.pos.xyz, 1), WorldM);\n"
    "    o.hpos = mul(p, ViewProjM);\n"
    "    o.pos = p.xyz;\n"
    "    float3 n = LightDir.w > 0 ? i.normal * LightDir.w : normalize(i.normal);\n"
    "    o.normal = i.normal;\n"
    "    o.refl = mul(n, (float3x3)ViewM).xy;\n"
    "    o.light.rgb = saturate(LightAmbient.rgb + LightDiffuse.rgb * max(dot(n, LightDir.xyz), 0));\n"
    "    float z = Fog.w > 0.5 ? o.hpos.w : o.hpos.z / o.hpos.w;\n"
    "    o.light.a = Fog.z * (1 - saturate((Fog.y - z) / max(Fog.y - Fog.x, 1e-6)));\n"
    "    o.mask = i.mask;\n"
    "    o.uv = i.uv;\n"
    "    return o;\n"
    "}\n";

// The depth (INTZ, point read) into a texture of its own: the water's and the effects' DepthTex while the picture's
// depth is still drawn with.
constexpr const char *copy_ps =
    "sampler2D Depth : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(Depth, uv).rrrr; }\n";

struct Vertex {
    float x, y, z;
    float u, v;
};
constexpr std::array<Vertex, 4> quad{{{-1, 1, 0, 0, 0}, {1, 1, 0, 1, 0}, {-1, -1, 0, 0, 1}, {1, -1, 0, 1, 1}}};

// The pack's files for #include.
class Include : public ID3DXInclude {
public:
    explicit Include(const ShaderPacks::Files &files) : m_Files(files) {}
    HRESULT WINAPI Open(D3DXINCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID *data, UINT *bytes) override {
        std::wstring wide;
        try {
            wide = utils::to_wstring(name);
        }
        catch (...) {
            return E_FAIL;
        }
        const auto content = ShaderPacks::file(m_Files, wide);
        if (!content) return E_FAIL;
        m_Open.push_back(*content);
        *data = m_Open.back().data();
        *bytes = UINT(m_Open.back().size());
        return S_OK;
    }
    HRESULT WINAPI Close(LPCVOID) override { return S_OK; }

private:
    const ShaderPacks::Files &m_Files;
    std::deque<std::string> m_Open;  // (a deque: a text stays where it is as more come; a short one in a vector moved)
};

struct PassCode {
    IDirect3DPixelShader9 *shader = nullptr;
    int output = -1;  // a target's index, -1: the screen
    ShaderPacks::Inputs inputs;  // the pictures its code reads, at picture_sampler and on
};

struct State {
    std::uint32_t version = 0;  // ShaderPacks::choice_version() built (0: nothing)
    bool ready = false;         // the pack's code and textures are built
    ShaderPacks::Pack pack;
    IDirect3DVertexShader9 *vertex = nullptr;
    std::vector<PassCode> passes;
    std::vector<IDirect3DTexture9 *> textures;  // managed: they outlive a lost device
    IDirect3DTexture9 *white = nullptr;         // the depth where the card cannot give one: all far
    // The default pool's, for the screen's size.
    UINT width = 0, height = 0;
    IDirect3DTexture9 *scene = nullptr, *depth = nullptr, *solid = nullptr;
    IDirect3DSurface9 *scene_surface = nullptr, *depth_surface = nullptr, *solid_surface = nullptr;
    bool solid_taken = false;  // this frame's: before_effects came
    std::vector<IDirect3DTexture9 *> targets;
    std::vector<IDirect3DSurface9 *> target_surfaces;
    // The shadow map (the pack's Shadow=, while its setting has it on): a depth texture of its own size, and the
    // colour target Direct3D 9 wants beside it. shadow_begin's of this frame: where it stood.
    int shadow_size = 0;  // 0: none asked for
    IDirect3DTexture9 *shadow = nullptr;
    IDirect3DSurface9 *shadow_surface = nullptr, *shadow_color = nullptr;
    bool shadow_drawn = false;
    D3DXMATRIX shadow_view_proj;
    ShaderShadow::Fit shadow_fit;  // this frame's (shadow_keeps)
    float shadow_texel = 0, shadow_depth = 0;
    D3DVIEWPORT9 viewport{};  // the picture's, while the map is drawn
    // The world pass (the pack's World=, while its setting has it on): its pixel shader, the targets it draws
    // into (in targets), a vertex shader for each vertex format the world's drawing uses (null: one that failed),
    // a depth of its own (the game's fixed pipeline rounds a vertex otherwise: its depth would not match).
    IDirect3DPixelShader9 *world = nullptr;
    std::vector<int> world_outputs;
    std::map<DWORD, IDirect3DVertexShader9 *> world_vertices;
    IDirect3DSurface9 *world_depth = nullptr;
    D3DXMATRIX world_view_proj;
    float world_plane[4]{};  // the clip plane as the hook left it (the game's, into the shader's space)
    // Motion: each draw's world matrix by the thing drawn and the draw's number within it, this frame's and the
    // previous one's (a thing's parts are drawn in the same order frame after frame).
    std::map<std::pair<const void *, int>, D3DXMATRIX> world_now, world_before;
    DWORD world_write = 0x100;  // the further targets' colour writes as world_hook last set them
    const void *world_who = nullptr;  // the thing being drawn (null: the land, which does not move)
    int world_index = 0;              // its draws so far
    // The land's normals as the map keeps them at its points, a texel a point (managed): for the world pass.
    IDirect3DTexture9 *land = nullptr;
    const void *land_map = nullptr;
    int land_width = 0, land_height = 0;
    bool world_plane_set = false;
    // The water by the pack (Water=): the engine's vertex shader for the water's vertices and the pack's pixel
    // shader; the picture and the depth just before it (UnderTex, DepthTex: copies, the picture's own are drawn into).
    IDirect3DVertexShader9 *water_vertex = nullptr;
    IDirect3DPixelShader9 *water = nullptr;
    IDirect3DTexture9 *under = nullptr, *depth_copy = nullptr;
    IDirect3DSurface9 *under_surface = nullptr, *depth_copy_surface = nullptr;
    IDirect3DPixelShader9 *copy = nullptr;  // the depth (INTZ) into depth_copy
    D3DXVECTOR4 water_light{0, 0, 0, 0};  // towards the game's light (w: the normal's factor, set by each part)
    // The effects by the pack (Effects=): its code (the prelude, its file) and its files for #include, the profile
    // the card takes (shader model 2: the game's vertex processing goes on); a pixel shader for each kind of draw
    // met (by its stages; null: one the code cannot do).
    std::string effects_source;
    std::shared_ptr<const ShaderPacks::Files> files;
    const char *effects_profile = nullptr;
    std::map<std::string, IDirect3DPixelShader9 *> effects;
    // A kind's code being compiled beside the drawing (one at a time; meanwhile such effects are the game's): its
    // stages' key, the compiled code or the compiler's words, since when.
    std::string effects_key;
    std::future<std::pair<std::string, std::string>> effects_build;
    std::chrono::steady_clock::time_point effects_started;
    bool effects_drawing = false;
    IDirect3DStateBlock9 *saved = nullptr;
    // This frame's: the screen and its depth, given back after the passes.
    bool taken = false;
    IDirect3DSurface9 *screen = nullptr, *screen_depth = nullptr;
    D3DXMATRIX previous_view_proj;
    D3DXVECTOR3 previous_eye{0, 0, 0};
    bool has_previous = false;
    DWORD started = 0;
    DWORD last_frame = 0;  // timeGetTime() of the previous frame's passes
    float frame = 0;
    float history = 0;  // frames drawn since the targets were made (what they keep is the previous frame's)
    std::wstring problem;
};
State g_state;

// Measuring (time_begin): a set of the card's timestamps for each of the last frames (read a few frames later, when
// the card has them: no waiting), and the sums of the frames read.
const bool g_timing = [] {
    const char *value = std::getenv("PBO_SHADER_TIMING");
    return value && value[0] == '1';
}();
constexpr int timing_sets = 4;
constexpr int timing_parts = int(Part::Count);
struct TimingSet {
    IDirect3DQuery9 *disjoint = nullptr, *frequency = nullptr;
    IDirect3DQuery9 *begin[timing_parts]{}, *end[timing_parts]{};
    bool used[timing_parts]{};
    bool issued = false;
};
struct Timing {
    TimingSet sets[timing_sets];
    int now = 0;  // this frame's set
    double sums[timing_parts]{};
    int counts[timing_parts]{};
    int frames = 0;
    DWORD started = 0;  // timeGetTime() of the first frame summed
    bool broken = false;  // the card gives no timestamps
    // The lights over the same frames: the most a frame collected, of them the camera might see, frames that had
    // more than the collection keeps.
    std::size_t lights_most = 0, lights_seen_most = 0;
    int lights_full = 0;
    int water_parts = 0, water_parts_most = 0;  // the water's parts this frame, the most of a frame
};
Timing g_timing_state;

void timing_drop() {
    for (auto &set : g_timing_state.sets) {
        if (set.disjoint) set.disjoint->Release();
        if (set.frequency) set.frequency->Release();
        set.disjoint = set.frequency = nullptr;
        for (int i = 0; i < timing_parts; ++i) {
            if (set.begin[i]) set.begin[i]->Release();
            if (set.end[i]) set.end[i]->Release();
            set.begin[i] = set.end[i] = nullptr;
            set.used[i] = false;
        }
        set.issued = false;
    }
}

const char *part_name(int part) {
    static const char *const names[timing_parts] = {"shadow map", "world pass", "water", "effects", "passes", "frame"};
    return names[part];
}

std::size_t format_bytes(D3DFORMAT format) {
    switch (format) {
        case D3DFMT_A16B16G16R16F: return 8;
        case D3DFMT_R5G6B5: return 2;
        default: return 4;
    }
}

// The voxel copy of the map (the pack's Voxels=, while its setting has it on): the grid, its 3D texture (managed: it
// outlives a lost device), the things still to be put into it (a few each frame).
struct VoxelCopy {
    float wanted = 0;  // the pack's voxel size (0: none)
    float built = 0;   // the size the grid was made for
    const void *map = nullptr;
    ShaderVoxels::Grid grid;
    IDirect3DVolumeTexture9 *texture = nullptr;
    int tx = 0, ty = 0, tz = 0;  // the texture's sides (powers of two over the grid's)
    std::deque<CMatrixMapStatic *> queue;
    std::unordered_set<const CMatrixMapStatic *> queued;
    int scan_in = 0;       // frames till the map's things are looked over again (new ones)
    bool refused = false;  // the card has no such textures (said once)
    bool complete = false;  // the map's things all in it once (then what comes is built later)
    double build_ms = 0;    // the time its things took, and over how many frames (measured)
    int build_frames = 0;
};
VoxelCopy g_voxels;

void voxels_drop() {
    drop(g_voxels.texture);
    g_voxels.grid.reset({});
    g_voxels.queue.clear();
    g_voxels.queued.clear();
    g_voxels.map = nullptr;
    g_voxels.built = 0;
    g_voxels.complete = false;
    g_voxels.build_ms = 0;
    g_voxels.build_frames = 0;
}

// The game settings the drawn pack decides (ShaderPacks::game_settings).
bool g_game_fog = true;
bool g_shadows_decided = false;  // g_Config's shadows are the pack's: the person's come back when it stops

void decide_game_settings(bool drawing) {
    const auto decided = [&](const char *name) -> std::optional<bool> {
        if (!drawing) return std::nullopt;
        const auto found = g_state.pack.game.find(name);
        return found == g_state.pack.game.end() ? std::nullopt : std::optional<bool>(found->second);
    };
    g_game_fog = decided("Fog").value_or(true);
    const auto stencil = decided("StencilShadows"), projected = decided("ProjectedShadows");
    if (!stencil && !projected && !g_shadows_decided) return;  // (the person's, as the settings or the console set)
    const auto &values = StandaloneSettings::current();
    g_Config.m_ShowStencilShadows = stencil.value_or(values.stencil_shadows);
    g_Config.m_ShowProjShadows = projected.value_or(values.projected_shadows);
    g_shadows_decided = stencil || projected;
}

void release_frame_surfaces() {
    drop(g_state.screen);
    drop(g_state.screen_depth);
    g_state.taken = false;
}

void release_default() {
    timing_drop();
    release_frame_surfaces();
    drop(g_state.scene_surface);
    drop(g_state.depth_surface);
    drop(g_state.solid_surface);
    drop(g_state.scene);
    drop(g_state.depth);
    drop(g_state.solid);
    for (auto *&surface : g_state.target_surfaces) drop(surface);
    for (auto *&texture : g_state.targets) drop(texture);
    g_state.target_surfaces.clear();
    g_state.targets.clear();
    drop(g_state.shadow_surface);
    drop(g_state.shadow_color);
    drop(g_state.shadow);
    g_state.shadow_drawn = false;
    drop(g_state.world_depth);
    drop(g_state.under_surface);
    drop(g_state.under);
    drop(g_state.depth_copy_surface);
    drop(g_state.depth_copy);
    drop(g_state.saved);
    g_state.width = g_state.height = 0;
    g_state.has_previous = false;
    g_state.history = 0;
}

void release_code() {
    for (auto &pass : g_state.passes) drop(pass.shader);
    g_state.passes.clear();
    for (auto *&texture : g_state.textures) drop(texture);
    g_state.textures.clear();
    drop(g_state.vertex);
    drop(g_state.white);
    drop(g_state.world);
    drop(g_state.land);
    g_state.land_map = nullptr;
    g_state.world_now.clear();
    g_state.world_before.clear();
    for (auto &[format, shader] : g_state.world_vertices) drop(shader);
    g_state.world_vertices.clear();
    g_state.world_outputs.clear();
    drop(g_state.water_vertex);
    drop(g_state.water);
    drop(g_state.copy);
    if (g_state.effects_build.valid()) g_state.effects_build.wait();
    g_state.effects_build = {};
    for (auto &[key, shader] : g_state.effects) drop(shader);
    g_state.effects.clear();
    g_state.effects_source.clear();
    g_state.effects_profile = nullptr;
    g_state.files.reset();
    g_state.ready = false;
}

void fail(std::wstring why) {
    lgr.error("Shaders: {}")(utils::from_wstring(why));
    g_state.problem = std::move(why);
    release_code();
    release_default();
}

std::wstring wide(const std::string &text) {
    try {
        return utils::to_wstring(text);
    }
    catch (...) {
        return std::wstring(text.begin(), text.end());
    }
}

std::optional<std::string> compile(const std::string &source, const char *name, const char *entry, const char *profile,
                                   ID3DXInclude *include, std::wstring &error) {
    ID3DXBuffer *code = nullptr, *messages = nullptr;
    const HRESULT result = D3DXCompileShader(source.data(), UINT(source.size()), nullptr, include, entry, profile, 0,
                                             &code, &messages, nullptr);
    std::string text;
    if (messages) text.assign(static_cast<const char *>(messages->GetBufferPointer()), messages->GetBufferSize());
    drop(messages);
    if (FAILED(result) || !code) {
        drop(code);
        while (!text.empty() && (text.back() == '\0' || text.back() == '\n' || text.back() == '\r')) text.pop_back();
        // The first error is enough for the settings' line; test.log has them all.
        lgr.error("Shaders: {} did not compile:\n{}")(name, text);
        const auto line_end = text.find('\n');
        error = wide(line_end == std::string::npos ? text : text.substr(0, line_end));  // (it names the file)
        return std::nullopt;
    }
    std::string bytes(static_cast<const char *>(code->GetBufferPointer()), code->GetBufferSize());
    drop(code);
    return bytes;
}

D3DFORMAT d3d_format(ShaderPacks::Format format) {
    switch (format) {
        case ShaderPacks::Format::RGBA16F: return D3DFMT_A16B16G16R16F;
        case ShaderPacks::Format::RG16F: return D3DFMT_G16R16F;
        case ShaderPacks::Format::R32F: return D3DFMT_R32F;
        default: return D3DFMT_A8R8G8B8;
    }
}

D3DFORMAT display_format() {
    D3DDISPLAYMODE mode{};
    if (FAILED(g_D3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode))) return D3DFMT_X8R8G8B8;
    return mode.Format;
}

bool supported(D3DFORMAT format, DWORD usage) {
    return SUCCEEDED(g_D3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display_format(), usage,
                                              D3DRTYPE_TEXTURE, format));
}

// The chosen pack's code and textures; false (and problem) when it cannot be.
bool build() {
    release_code();
    release_default();
    g_state.problem.clear();
    const auto &choice = ShaderPacks::choice();
    const ShaderPacks::Pack *pack = ShaderPacks::find(ShaderPacks::installed(), choice.pack);
    if (!pack) {
        fail(L"no pack " + choice.pack + L" in shaders");
        return false;
    }
    if (!pack->problem.empty()) {
        fail(pack->id + L": " + pack->problem);
        return false;
    }
    if (g_D3DDCaps.PixelShaderVersion < D3DPS_VERSION(3, 0) || g_D3DDCaps.VertexShaderVersion < D3DVS_VERSION(3, 0)) {
        fail(L"the video card has no Shader Model 3");
        return false;
    }
    g_state.pack = *pack;
    g_voxels.wanted = ShaderPacks::voxels_on(*pack, choice.settings) ? pack->voxels.size : 0;
    g_state.shadow_size = ShaderPacks::shadow_on(*pack, choice.settings) ? pack->shadow.size : 0;
    std::string error;
    const auto files = ShaderPacks::load_files(std::filesystem::path(ShaderPacks::folder) / pack->id, error);
    if (!files) {
        fail(pack->id + L": " + wide(error));
        return false;
    }
    std::wstring problem;
    const auto vertex = compile(quad_vs, "quad", "main", "vs_3_0", nullptr, problem);
    if (!vertex || FAILED(g_D3DD->CreateVertexShader(reinterpret_cast<const DWORD *>(vertex->data()), &g_state.vertex))) {
        fail(L"the engine's vertex shader: " + problem);
        return false;
    }
    Include include(*files);
    for (const auto &pass : pack->passes) {
        const std::string name = utils::from_wstring(pass.file);
        // (the scan saw the file; the pack may have changed on disk since)
        const auto text = ShaderPacks::file(*files, pass.file);
        if (!text) {
            fail(pack->id + L": no " + pass.file);
            return false;
        }
        PassCode built;
        built.inputs = ShaderPacks::pass_inputs(*pack, *files, pass);
        if (int(built.inputs.size()) > ShaderPacks::max_pass_pictures) {
            fail(pass.file + L" reads more than " + std::to_wstring(ShaderPacks::max_pass_pictures) +
                 L" targets and textures");
            return false;
        }
        const std::string source = ShaderPacks::prelude(*pack, choice.settings, built.inputs) + "#line 1 \"" + name +
                                   "\"\n" + *text;
        const auto code = compile(source, name.c_str(), pass.entry.c_str(), "ps_3_0", &include, problem);
        if (!code ||
            FAILED(g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(code->data()), &built.shader))) {
            fail(problem.empty() ? wide(name) + L": the video card refused it" : problem);
            return false;
        }
        if (pass.output != "screen")
            built.output = int(std::ranges::find(pack->targets, pass.output, &ShaderPacks::Target::name) -
                               pack->targets.begin());
        g_state.passes.push_back(built);
    }
    // The world pass: the pack's code between the engine's prelude and the entry that calls its main.
    if (ShaderPacks::world_on(*pack, choice.settings)) {
        const std::string name = utils::from_wstring(pack->world.file);
        const auto text = ShaderPacks::file(*files, pack->world.file);
        if (!text) {
            fail(pack->id + L": no " + pack->world.file);
            return false;
        }
        if (g_D3DDCaps.NumSimultaneousRTs < pack->world.outputs.size())
            lgr.warning("Shaders: the video card draws into {} targets at once, the world pass wants {}: drawn without it")(
                    g_D3DDCaps.NumSimultaneousRTs, pack->world.outputs.size());
        else {
            const std::string source = ShaderPacks::world_prelude(*pack, choice.settings) + "#line 1 \"" + name +
                                       "\"\n" + *text + ShaderPacks::world_wrapper(*pack);
            const auto code = compile(source, name.c_str(), ShaderPacks::world_entry, "ps_3_0", &include, problem);
            if (!code ||
                FAILED(g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(code->data()), &g_state.world))) {
                fail(problem.empty() ? wide(name) + L": the video card refused it" : problem);
                return false;
            }
            for (const auto &output : pack->world.outputs)
                g_state.world_outputs.push_back(int(
                        std::ranges::find(pack->targets, output, &ShaderPacks::Target::name) - pack->targets.begin()));
        }
    }
    // The water: the engine's vertex shader (the game's light on it, its reflection's place, its fog), the pack's
    // code between the prelude and the entry that calls its main.
    if (ShaderPacks::water_on(*pack, choice.settings)) {
        const std::string name = utils::from_wstring(pack->water.file);
        const auto text = ShaderPacks::file(*files, pack->water.file);
        const auto vertex_code = compile(water_vs, "water vertex", "main", "vs_3_0", nullptr, problem);
        if (!vertex_code || FAILED(g_D3DD->CreateVertexShader(reinterpret_cast<const DWORD *>(vertex_code->data()),
                                                              &g_state.water_vertex))) {
            fail(L"the engine's water vertex shader: " + problem);
            return false;
        }
        const auto code = text ? compile(ShaderPacks::water_prelude(*pack, choice.settings) + "#line 1 \"" + name +
                                                 "\"\n" + *text + ShaderPacks::water_wrapper(),
                                         name.c_str(), ShaderPacks::water_entry, "ps_3_0", &include, problem)
                               : std::optional<std::string>{};
        if (!code || FAILED(g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(code->data()), &g_state.water))) {
            fail(!text ? pack->id + L": no " + pack->water.file
                       : problem.empty() ? wide(name) + L": the video card refused it" : problem);
            return false;
        }
    }
    // The effects: built for each kind of draw as it comes; once now (a plain one) so that an error is said at once,
    // in the first shader model 2 profile the card takes.
    if (ShaderPacks::effects_on(*pack, choice.settings)) {
        const std::string name = utils::from_wstring(pack->effects.file);
        const auto text = ShaderPacks::file(*files, pack->effects.file);
        if (!text) {
            fail(pack->id + L": no " + pack->effects.file);
            return false;
        }
        g_state.effects_source = ShaderPacks::effects_prelude(*pack, choice.settings) + "#line 1 \"" + name + "\"\n" + *text;
        ShaderPacks::Stages plain{};
        plain[0] = {D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE, false};
        const auto combine = ShaderPacks::combiner(plain);
        const std::string source = g_state.effects_source + ShaderPacks::effects_wrapper(plain, *combine);
        std::wstring first_problem;
        for (const char *profile : {"ps_2_a", "ps_2_b", "ps_2_0"}) {
            const auto code = compile(source, name.c_str(), ShaderPacks::effects_entry, profile, &include, problem);
            if (first_problem.empty()) first_problem = problem;
            IDirect3DPixelShader9 *shader = nullptr;
            if (code && SUCCEEDED(g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(code->data()), &shader))) {
                drop(shader);
                g_state.effects_profile = profile;
                break;
            }
        }
        if (!g_state.effects_profile) {
            fail(first_problem.empty() ? wide(name) + L": the video card refused it" : first_problem);
            return false;
        }
        lgr.notice("Shaders: the effects in {}")(g_state.effects_profile);
    }
    if (g_state.water || g_state.effects_profile) {
        const auto code = compile(copy_ps, "depth copy", "main", "ps_3_0", nullptr, problem);
        if (code) g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(code->data()), &g_state.copy);
        g_state.files = std::make_shared<const ShaderPacks::Files>(*files);
    }
    for (const auto &texture : pack->textures) {
        const auto found = files->find(texture.file);
        if (found == files->end()) {
            fail(pack->id + L": no " + texture.file);
            return false;
        }
        const std::string &bytes = found->second;
        IDirect3DTexture9 *made = nullptr;
        if (FAILED(D3DXCreateTextureFromFileInMemoryEx(g_D3DD, bytes.data(), UINT(bytes.size()), D3DX_DEFAULT_NONPOW2,
                                                       D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
                                                       D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr,
                                                       &made))) {
            fail(texture.file + L": not an image the game can read");
            return false;
        }
        g_state.textures.push_back(made);
    }
    if (SUCCEEDED(g_D3DD->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_state.white, nullptr))) {
        D3DLOCKED_RECT locked;
        if (SUCCEEDED(g_state.white->LockRect(0, &locked, nullptr, 0))) {
            *static_cast<DWORD *>(locked.pBits) = 0xffffffff;
            g_state.white->UnlockRect(0);
        }
    }
    g_state.ready = true;
    lgr.notice("Shaders: {} ({} passes) built")(utils::from_wstring(pack->id), g_state.passes.size());
    return true;
}

// The shadow map's depth texture and the colour target beside it (one that keeps nothing where the card has such;
// else the smallest it gives). Without them the pack draws as with no map (ShadowInfo.w 0).
void make_shadow() {
    if (g_state.shadow_size <= 0) return;
    const UINT size = std::min({UINT(g_state.shadow_size), UINT(g_D3DDCaps.MaxTextureWidth),
                                UINT(g_D3DDCaps.MaxTextureHeight)});
    if (!supported(intz, D3DUSAGE_DEPTHSTENCIL) ||
        FAILED(g_D3DD->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL, intz, D3DPOOL_DEFAULT, &g_state.shadow,
                                     nullptr)) ||
        FAILED(g_state.shadow->GetSurfaceLevel(0, &g_state.shadow_surface))) {
        drop(g_state.shadow_surface);
        drop(g_state.shadow);
        lgr.warning("Shaders: no depth texture for the shadow map: drawn without it");
        return;
    }
    for (const D3DFORMAT format : {null_format, D3DFMT_R5G6B5, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8}) {
        if (FAILED(g_D3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display_format(), D3DUSAGE_RENDERTARGET,
                                            D3DRTYPE_SURFACE, format)))
            continue;
        if (SUCCEEDED(g_D3DD->CreateRenderTarget(size, size, format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                 &g_state.shadow_color, nullptr)))
            break;
        drop(g_state.shadow_color);
    }
    if (!g_state.shadow_color) {
        drop(g_state.shadow_surface);
        drop(g_state.shadow);
        lgr.warning("Shaders: no target for the shadow map: drawn without it");
        return;
    }
    g_state.shadow_size = int(size);
}

// The textures of the screen's size; false (and problem) when the card cannot make them.
bool make_surfaces(UINT width, UINT height) {
    release_default();
    // The picture in half floats where the card blends into them: what the effects add over a bright place (a day's
    // sky) is kept whole, not cut at white, so a pack sees it as over anything else (8 bits: as before).
    // (and reads them smoothly: the passes do)
    const D3DFORMAT picture = supported(D3DFMT_A16B16G16R16F, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER |
                                                                      D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)
                                      ? D3DFMT_A16B16G16R16F
                                      : D3DFMT_A8R8G8B8;
    if (FAILED(g_D3DD->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, picture, D3DPOOL_DEFAULT,
                                     &g_state.scene, nullptr)) ||
        FAILED(g_state.scene->GetSurfaceLevel(0, &g_state.scene_surface))) {
        fail(L"no texture for the picture (video memory?)");
        return false;
    }
    if (FAILED(g_D3DD->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, picture, D3DPOOL_DEFAULT,
                                     &g_state.solid, nullptr)) ||
        FAILED(g_state.solid->GetSurfaceLevel(0, &g_state.solid_surface))) {
        fail(L"no texture for the picture without the effects (video memory?)");
        return false;
    }
    // The depth the passes read (INTZ: every card of the last 15 years); without it a depth surface of our own.
    if (supported(intz, D3DUSAGE_DEPTHSTENCIL) &&
        SUCCEEDED(g_D3DD->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, intz, D3DPOOL_DEFAULT, &g_state.depth,
                                        nullptr)))
        g_state.depth->GetSurfaceLevel(0, &g_state.depth_surface);
    else {
        drop(g_state.depth);
        lgr.warning("Shaders: the video card gives no depth texture (INTZ): passes see everything far");
        if (FAILED(g_D3DD->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                     &g_state.depth_surface, nullptr))) {
            fail(L"no depth surface for the picture");
            return false;
        }
    }
    for (const auto &target : g_state.pack.targets) {
        // (read smoothly by the passes: a format the card cannot filter would read as nothing)
        D3DFORMAT format = d3d_format(target.format);
        if (!supported(format, D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_FILTER)) format = D3DFMT_A8R8G8B8;
        const UINT w = target.width ? UINT(target.width) : std::max(1u, UINT(float(width) * target.scale + 0.5f));
        const UINT h = target.height ? UINT(target.height) : std::max(1u, UINT(float(height) * target.scale + 0.5f));
        IDirect3DTexture9 *texture = nullptr;
        IDirect3DSurface9 *surface = nullptr;
        if (FAILED(g_D3DD->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &texture, nullptr)) ||
            FAILED(texture->GetSurfaceLevel(0, &surface))) {
            drop(texture);
            fail(L"no texture for the target " + wide(target.name) + L" (video memory?)");
            return false;
        }
        g_state.targets.push_back(texture);
        g_state.target_surfaces.push_back(surface);
        // Black at first: a pass that reads what a target kept from the previous frame finds nothing there.
        g_D3DD->SetRenderTarget(0, surface);
        g_D3DD->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    }
    if (FAILED(g_D3DD->CreateStateBlock(D3DSBT_ALL, &g_state.saved))) {
        fail(L"no state block");
        return false;
    }
    make_shadow();
    // The water's and the effects' copies: the picture before the water, the depth.
    if (g_state.water && (FAILED(g_D3DD->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, picture, D3DPOOL_DEFAULT,
                                                       &g_state.under, nullptr)) ||
                          FAILED(g_state.under->GetSurfaceLevel(0, &g_state.under_surface)))) {
        drop(g_state.under);
        lgr.warning("Shaders: no texture for the picture under the water: the water sees black there");
    }
    if ((g_state.water || g_state.effects_profile) && g_state.depth && g_state.copy &&
        supported(D3DFMT_R32F, D3DUSAGE_RENDERTARGET) &&
        (FAILED(g_D3DD->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                                      &g_state.depth_copy, nullptr)) ||
         FAILED(g_state.depth_copy->GetSurfaceLevel(0, &g_state.depth_copy_surface)))) {
        drop(g_state.depth_copy);
        lgr.warning("Shaders: no texture for a copy of the depth: the water and the effects see everything far");
    }
    if (g_state.world) {
        for (const D3DFORMAT format : {D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16})
            if (SUCCEEDED(g_D3DD->CreateDepthStencilSurface(width, height, format, D3DMULTISAMPLE_NONE, 0, TRUE,
                                                            &g_state.world_depth, nullptr)))
                break;
        if (!g_state.world_depth) lgr.warning("Shaders: no depth surface for the world pass: drawn without it");
    }
    g_state.width = width;
    g_state.height = height;
    if (g_timing) {
        // The video memory of the textures the pack draws with (the default pool's).
        const std::size_t pixels = std::size_t(width) * height;
        std::size_t bytes = pixels * (2 * format_bytes(picture) + 4);  // the picture, SolidTex, the depth
        for (auto *texture : g_state.targets) {
            D3DSURFACE_DESC desc{};
            texture->GetLevelDesc(0, &desc);
            bytes += std::size_t(desc.Width) * desc.Height * format_bytes(desc.Format);
        }
        if (g_state.shadow) bytes += std::size_t(g_state.shadow_size) * g_state.shadow_size * 4;
        if (g_state.world_depth) bytes += pixels * 4;
        if (g_state.under) bytes += pixels * format_bytes(picture);
        if (g_state.depth_copy) bytes += pixels * 4;
        lgr.notice("Shaders: {}x{}: the pack's textures {:.1f} MB of video memory")(width, height,
                                                                                   double(bytes) / (1024.0 * 1024.0));
    }
    return true;
}

D3DXVECTOR4 color(DWORD argb, float w = 0) {
    return {float((argb >> 16) & 0xff) / 255.0f, float((argb >> 8) & 0xff) / 255.0f, float(argb & 0xff) / 255.0f, w};
}

// The colour of the map's sky just above the horizon: its skybox's four sides averaged over the band a camera sees
// there (DrawSky draws a side down to about 0.55 of its height), the lower the more. A map's sky colour (its fog's)
// may be far from its sky (Module: a dark brown under a bright orange one). w: 1 from the skybox, 0 the sky colour
// standing in (no skybox, or its pictures in a format not read here). Worked out once per map and sky.
struct Horizon {
    const void *key[6]{};
    D3DXVECTOR4 color{0, 0, 0, 0};
    int tries = 0;  // frames its skybox could not be read (a texture lost just then): tried again for a while
};
Horizon g_horizon;

void update_horizon(CMatrixMap &map) {
    const void *key[6] = {&map, map.m_SkyTex[0].tex, map.m_SkyTex[1].tex, map.m_SkyTex[2].tex, map.m_SkyTex[3].tex,
                          reinterpret_cast<const void *>(std::uintptr_t(map.m_SkyColor) ^
                                                         (std::uintptr_t(g_Config.m_SkyBox) << 24))};
    if (std::equal(std::begin(key), std::end(key), std::begin(g_horizon.key))) {
        if (g_horizon.tries <= 0 || g_horizon.tries > 120) return;
    }
    else g_horizon.tries = 0;
    std::copy(std::begin(key), std::end(key), std::begin(g_horizon.key));
    g_horizon.color = color(map.m_SkyColor, 0);
    D3DXVECTOR3 sum{0, 0, 0};
    float weight = 0;
    bool sides = false;
    for (const SSkyTex &side : map.m_SkyTex) {
        if (g_Config.m_SkyBox == 0 || !side.tex) continue;
        sides = true;
        IDirect3DTexture9 *texture = side.tex->Tex();
        IDirect3DSurface9 *source = nullptr, *copy = nullptr;
        D3DSURFACE_DESC desc{};
        // A plain 32-bit copy of it in system memory, whatever its format (the low quality sky's: compressed).
        if (!texture || FAILED(texture->GetSurfaceLevel(0, &source)) || FAILED(source->GetDesc(&desc)) ||
            FAILED(g_D3DD->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                       &copy, nullptr)) ||
            FAILED(D3DXLoadSurfaceFromSurface(copy, nullptr, nullptr, source, nullptr, nullptr, D3DX_FILTER_NONE, 0))) {
            drop(source);
            drop(copy);
            continue;
        }
        drop(source);
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            for (int j = 0; j < 16; ++j) {
                const float down = 0.40f + 0.16f * (float(j) + 0.5f) / 16.0f;  // of the side's height, from its top
                const float v = side.v0 + (side.v1 - side.v0) * down;
                const int y = std::clamp(int(v * float(desc.Height)), 0, int(desc.Height) - 1);
                const auto *row = reinterpret_cast<const DWORD *>(static_cast<const BYTE *>(locked.pBits) +
                                                                  std::size_t(y) * std::size_t(locked.Pitch));
                const float w = 0.5f + float(j) / 16.0f;
                for (int i = 0; i < 32; ++i) {
                    const float u = side.u0 + (side.u1 - side.u0) * (float(i) + 0.5f) / 32.0f;
                    const DWORD c = row[std::clamp(int(u * float(desc.Width)), 0, int(desc.Width) - 1)];
                    sum += D3DXVECTOR3(float((c >> 16) & 0xff), float((c >> 8) & 0xff), float(c & 0xff)) * (w / 255.0f);
                    weight += w;
                }
            }
            copy->UnlockRect();
        }
        drop(copy);
    }
    if (weight > 0) {
        const D3DXVECTOR3 average = sum / weight;
        g_horizon.color = {average.x, average.y, average.z, 1};
    }
    // A skybox there but none of it read: again on the next frames (said once it is read, or given up).
    if (sides && weight <= 0 && ++g_horizon.tries <= 120) return;
    g_horizon.tries = weight > 0 ? 0 : 121;
    lgr.notice("Shaders: the sky at the horizon {:.2f} {:.2f} {:.2f} ({})")(
            g_horizon.color.x, g_horizon.color.y, g_horizon.color.z, weight > 0 ? "its skybox" : "its sky colour");
}

void constants(const CMatrixMap &map) {
    const auto set = [](int reg, const D3DXVECTOR4 &value) { g_D3DD->SetPixelShaderConstantF(reg, &value.x, 1); };
    const auto matrix = [](int reg, const D3DXMATRIX &value) { g_D3DD->SetPixelShaderConstantF(reg, &value._11, 4); };
    const auto &camera = map.m_Camera;
    const D3DXMATRIX &view = camera.GetViewMatrix();
    const D3DXMATRIX &proj = camera.GetProjMatrix();
    D3DXMATRIX view_proj = view * proj, inverse;
    if (!D3DXMatrixInverse(&inverse, nullptr, &view_proj)) D3DXMatrixIdentity(&inverse);
    const float w = float(g_state.width), h = float(g_state.height);
    set(1, {w, h, 1.0f / w, 1.0f / h});
    const DWORD now = timeGetTime();
    const float seconds = float(now - g_state.started) / 1000.0f;
    const float step = g_state.last_frame ? std::min(float(now - g_state.last_frame) / 1000.0f, 1.0f) : 0.0f;
    g_state.last_frame = now;
    set(2, {seconds, float(map.GetTime()) / 1000.0f, g_state.frame, step});
    const D3DXVECTOR3 &eye = camera.GetFrustumCenter();
    set(3, {eye.x, eye.y, eye.z, 1});
    set(4, {-map.m_LightMain.x, -map.m_LightMain.y, -map.m_LightMain.z, 0});
    set(5, color(map.m_LightMainColor, 1));
    set(6, color(map.m_AmbientColor, 1));
    const bool fog = FLAG(map.m_Flags, MMFLAG_FOG_ON);
    set(7, color(map.m_SkyColor, fog ? 1.0f : 0.0f));
    set(8, {float(MAX_VIEW_DISTANCE * FOG_NEAR_K), float(MAX_VIEW_DISTANCE * FOG_FAR_K), MAX_VIEW_DISTANCE, 0});
    const float near_plane = proj._33 != 0 ? -proj._43 / proj._33 : 1.0f;
    const float far_plane = proj._33 != 1 ? proj._43 / (1.0f - proj._33) : MAX_VIEW_DISTANCE;
    set(9, {proj._33, proj._43, near_plane, far_plane});
    set(10, {FLAG(map.m_Flags, MMFLAG_PAUSE) ? 1.0f : 0.0f, Mods::view_scale(), g_state.history,
             g_state.solid_taken ? 1.0f : 0.0f});
    set(11, {WATER_LEVEL, float(map.m_Size.x) * GLOBAL_SCALE, float(map.m_Size.y) * GLOBAL_SCALE, 0});
    set(130, g_horizon.color);
    // What the game's volumetric shadow laid where SolidTex.a is 0: the colour and its share (the map's shadow colour
    // as DrawShadows draws it, by its texture factor 0xC0).
    {
        const D3DXVECTOR4 shadow = color(map.m_ShadowColor, float((map.m_ShadowColor >> 24) & 0xff) / 255.0f);
        constexpr float factor = float(0xC0) / 255.0f;
        set(129, {shadow.x * factor, shadow.y * factor, shadow.z * factor, shadow.w * factor});
    }
    matrix(12, view);
    matrix(16, proj);
    matrix(20, view_proj);
    matrix(24, inverse);
    matrix(28, g_state.has_previous ? g_state.previous_view_proj : view_proj);
    const D3DXVECTOR3 before = g_state.has_previous ? g_state.previous_eye : eye;
    set(131, {before.x, before.y, before.z, 1});
    // The shadow map: a point of the world into it, its size, a pixel's and its depth's share of the world (w 0: no
    // map this frame).
    {
        D3DXMATRIX none;
        D3DXMatrixIdentity(&none);
        matrix(132, g_state.shadow_drawn ? g_state.shadow_view_proj : none);
        const float size = float(std::max(g_state.shadow_size, 1));
        set(136, {size, 1.0f / size, g_state.shadow_texel, g_state.shadow_drawn ? g_state.shadow_depth : 0.0f});
    }
    g_state.previous_eye = eye;
    g_state.previous_view_proj = view_proj;
    g_state.has_previous = true;

    // The voxel copy: its lowest corner and a voxel's size; a place into its texture (w 0: no copy); its size in
    // the world.
    {
        const auto &shape = g_voxels.grid.shape();
        const bool there = g_voxels.texture && shape.size > 0;
        set(139, {shape.x0, shape.y0, shape.z0, shape.size});
        set(140, there ? D3DXVECTOR4{1.0f / (float(g_voxels.tx) * shape.size), 1.0f / (float(g_voxels.ty) * shape.size),
                                     1.0f / (float(g_voxels.tz) * shape.size), 1.0f}
                       : D3DXVECTOR4{0, 0, 0, 0});
        set(141, {float(shape.nx) * shape.size, float(shape.ny) * shape.size, float(shape.nz) * shape.size, 0});
    }

    // The lights the camera may see, the strongest near it first.
    std::size_t seen = 0;
    const auto lights = ShaderLights::pick(ShaderLights::collected(), eye.x, eye.y, eye.z, ShaderLights::max_lights,
                                           [&](const ShaderLights::Light &light) {
                                               const bool in = camera.IsInFrustum(
                                                       D3DXVECTOR3(light.x, light.y, light.z), light.radius);
                                               seen += in;
                                               return in;
                                           });
    if (g_timing) {
        auto &t = g_timing_state;
        const std::size_t all = ShaderLights::collected().size() + ShaderLights::refused();
        t.lights_most = std::max(t.lights_most, all);
        t.lights_seen_most = std::max(t.lights_seen_most, seen);
        t.lights_full += ShaderLights::refused() > 0;
    }
    ShaderLights::clear();
    constexpr int n = ShaderLights::max_lights;
    std::array<D3DXVECTOR4, 1 + 3 * n> block{};
    block[0] = {float(lights.size()), float(n), 0, 0};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto &light = lights[i];
        block[1 + i] = {light.x, light.y, light.z, light.radius};
        block[1 + n + i] = {light.r, light.g, light.b, 1};
        block[1 + 2 * n + i] = {float(int(light.kind)), light.seed, 0, 0};
    }
    g_D3DD->SetPixelShaderConstantF(lights_register, &block[0].x, UINT(block.size()));
}

void sampler(int index, IDirect3DBaseTexture9 *texture, bool clamp, bool point) {
    g_D3DD->SetTexture(index, texture);
    const DWORD address = clamp ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP;
    const DWORD filter = point ? D3DTEXF_POINT : D3DTEXF_LINEAR;
    g_D3DD->SetSamplerState(index, D3DSAMP_ADDRESSU, address);
    g_D3DD->SetSamplerState(index, D3DSAMP_ADDRESSV, address);
    g_D3DD->SetSamplerState(index, D3DSAMP_ADDRESSW, address);
    g_D3DD->SetSamplerState(index, D3DSAMP_MINFILTER, filter);
    g_D3DD->SetSamplerState(index, D3DSAMP_MAGFILTER, filter);
    g_D3DD->SetSamplerState(index, D3DSAMP_MIPFILTER, point ? D3DTEXF_NONE : D3DTEXF_LINEAR);
    g_D3DD->SetSamplerState(index, D3DSAMP_SRGBTEXTURE, FALSE);
    g_D3DD->SetSamplerState(index, D3DSAMP_MAXMIPLEVEL, 0);
    g_D3DD->SetSamplerState(index, D3DSAMP_MIPMAPLODBIAS, 0);
}

// The land's height at a place, between the map's points (the map's edge beyond it).
float land_height(CMatrixMap &map, float wx, float wy) {
    const float fx = std::clamp(wx / GLOBAL_SCALE, 0.0f, float(map.m_Size.x));
    const float fy = std::clamp(wy / GLOBAL_SCALE, 0.0f, float(map.m_Size.y));
    const int x = std::min(int(fx), map.m_Size.x - 1), y = std::min(int(fy), map.m_Size.y - 1);
    const float tx = fx - float(x), ty = fy - float(y);
    const float a = map.PointGet(x, y)->z, b = map.PointGet(x + 1, y)->z;
    const float c = map.PointGet(x, y + 1)->z, d = map.PointGet(x + 1, y + 1)->z;
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

// A thing of the map into the grid: rays through it along each of the three axes, a voxel's width apart, by the
// thing's own test (the one a click on it goes by); every place a ray meets it is a voxel of it. Three ways: a wall
// a ray runs along is met by the rays across.
void voxelize(CMatrixMapStatic *object) {
    auto &grid = g_voxels.grid;
    const auto &shape = grid.shape();
    const D3DXVECTOR3 centre = object->GetGeoCenter();
    const float radius = std::min(object->GetRadius(), 600.0f);
    const float low[3] = {centre.x - radius, centre.y - radius, centre.z - radius};
    const float origin[3] = {shape.x0, shape.y0, shape.z0};
    const int sides[3] = {shape.nx, shape.ny, shape.nz};
    int from[3], to[3];
    for (int i = 0; i < 3; ++i) {
        from[i] = std::clamp(int(std::floor((low[i] - origin[i]) / shape.size)), 0, sides[i] - 1);
        to[i] = std::clamp(int(std::floor((low[i] + radius * 2 - origin[i]) / shape.size)), 0, sides[i] - 1);
    }
    std::vector<std::uint32_t> voxels;
    for (int axis = 0; axis < 3; ++axis) {
        const int a = (axis + 1) % 3, b = (axis + 2) % 3;
        D3DXVECTOR3 dir(0, 0, 0);
        (&dir.x)[axis] = 1.0f;
        const float start = origin[axis] + float(from[axis]) * shape.size - shape.size;
        const float end = origin[axis] + float(to[axis] + 1) * shape.size + shape.size;
        for (int ia = from[a]; ia <= to[a]; ++ia)
            for (int ib = from[b]; ib <= to[b]; ++ib) {
                D3DXVECTOR3 at;
                (&at.x)[axis] = start;
                (&at.x)[a] = origin[a] + (float(ia) + 0.5f) * shape.size;
                (&at.x)[b] = origin[b] + (float(ib) + 0.5f) * shape.size;
                for (int hit = 0; hit < 16; ++hit) {
                    float t = 1e20f;
                    if (!object->Pick(at, dir, &t) || !(t >= 0) || (&at.x)[axis] + t > end) break;
                    at += dir * t;
                    int x, y, z;
                    if (grid.voxel_of(at.x, at.y, at.z, x, y, z)) voxels.push_back(grid.index(x, y, z));
                    at += dir * (shape.size * 0.5f);
                }
            }
    }
    auto material = ShaderPacks::Material::Object;
    if (object->IsBuilding()) material = ShaderPacks::Material::Building;
    else if (object->IsCannon()) material = ShaderPacks::Material::Cannon;
    grid.add(object, std::uint8_t(material), std::move(voxels));
}

// The grid for this map (made once: the land at once, the things over the next frames), what is new on the map,
// and what changed into the texture.
void voxels_update(CMatrixMap &map) {
    auto &v = g_voxels;
    if (v.wanted <= 0) {
        if (v.map) voxels_drop();
        return;
    }
    if (v.refused) return;
    if (v.map != &map || v.built != v.wanted) {
        voxels_drop();
        if (!(g_D3DDCaps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) ||
            FAILED(g_D3D->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, display_format(), 0,
                                            D3DRTYPE_VOLUMETEXTURE, D3DFMT_L8))) {
            lgr.warning("Shaders: the video card has no 3D textures: no voxel copy");
            v.refused = true;
            return;
        }
        float low = 1e30f, high = -1e30f;
        for (int y = 0; y <= map.m_Size.y; ++y)
            for (int x = 0; x <= map.m_Size.x; ++x) {
                low = std::min(low, map.PointGet(x, y)->z);
                high = std::max(high, map.PointGet(x, y)->z);
            }
        low = std::max(low, float(WATER_LEVEL));  // (the land under the water: its surface is what counts)
        high = std::max(high, low);
        auto shape = ShaderVoxels::shape(float(map.m_Size.x) * GLOBAL_SCALE, float(map.m_Size.y) * GLOBAL_SCALE, low,
                                         high, v.wanted);
        const int tx = ShaderVoxels::texture_side(shape.nx), ty = ShaderVoxels::texture_side(shape.ny);
        const int tz = ShaderVoxels::texture_side(shape.nz);
        if (DWORD(std::max({tx, ty, tz})) > g_D3DDCaps.MaxVolumeExtent ||
            FAILED(g_D3DD->CreateVolumeTexture(UINT(tx), UINT(ty), UINT(tz), 1, 0, D3DFMT_L8, D3DPOOL_MANAGED,
                                               &v.texture, nullptr))) {
            lgr.warning("Shaders: no 3D texture of {}x{}x{} for the voxel copy")(tx, ty, tz);
            v.refused = true;
            return;
        }
        v.tx = tx, v.ty = ty, v.tz = tz;
        v.grid.reset(shape);
        for (int y = 0; y < shape.ny; ++y)
            for (int x = 0; x < shape.nx; ++x)
                v.grid.fill_land(x, y, land_height(map, (float(x) + 0.5f) * shape.size, (float(y) + 0.5f) * shape.size),
                                 std::uint8_t(ShaderPacks::Material::Land));
        v.map = &map;
        v.built = v.wanted;
        v.scan_in = 0;
        lgr.notice("Shaders: the voxel copy {}x{}x{}, a voxel {} of the world's units")(shape.nx, shape.ny, shape.nz,
                                                                                      shape.size);
    }
    // What stands on the map and is not in the grid yet (at first: everything; later: what was built).
    if (--v.scan_in <= 0) {
        v.scan_in = 30;
        for (int i = 0; i < map.m_GroupSize.x * map.m_GroupSize.y; ++i) {
            const CMatrixMapGroup *group = map.m_Group[i];
            if (!group) continue;
            for (int j = 0; j < group->ObjectsCnt(); ++j) {
                CMatrixMapStatic *object = group->GetObject(j);
                if (!object || object->IsDIP() || !object->IsVisible()) continue;
                const auto type = object->GetObjectType();
                if (type != OBJECT_TYPE_MAPOBJECT && type != OBJECT_TYPE_BUILDING && type != OBJECT_TYPE_CANNON) continue;
                if (v.grid.has(object) || !v.queued.insert(object).second) continue;
                v.queue.push_back(object);
            }
        }
    }
    // A couple of milliseconds of them a frame.
    const auto started = std::chrono::steady_clock::now();
    const bool had = !v.queue.empty();
    while (!v.queue.empty() && std::chrono::steady_clock::now() - started < std::chrono::milliseconds(2)) {
        CMatrixMapStatic *object = v.queue.front();
        v.queue.pop_front();
        v.queued.erase(object);
        voxelize(object);
        if (g_timing && v.complete)
            lgr.notice("Shaders: voxels: {} added ({} things)")(object->IsBuilding() ? "a building"
                                                                : object->IsCannon() ? "a turret"
                                                                                     : "an object", v.grid.things());
    }
    if (had) {
        v.build_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        v.build_frames += 1;
    }
    if (had && v.queue.empty() && !v.complete) {
        v.complete = true;
        const auto &shape = v.grid.shape();
        lgr.notice("Shaders: the voxel copy has the map's {} things: {:.0f} ms of building over {} frames; {:.1f} MB "
                   "(the grid {:.1f}, the texture {:.1f} twice: the game's copy and the card's)")(
                v.grid.things(), v.build_ms, v.build_frames,
                (double(v.grid.memory()) + 2.0 * double(v.tx) * v.ty * v.tz) / (1024.0 * 1024.0),
                double(v.grid.memory()) / (1024.0 * 1024.0), double(v.tx) * v.ty * v.tz / (1024.0 * 1024.0));
        (void)shape;
    }
    const auto dirty = v.grid.take_dirty();
    if (dirty.empty()) return;
    const D3DBOX box{UINT(dirty.x0), UINT(dirty.y0), UINT(dirty.x1 + 1), UINT(dirty.y1 + 1), UINT(dirty.z0),
                     UINT(dirty.z1 + 1)};
    D3DLOCKED_BOX locked;
    if (FAILED(v.texture->LockBox(0, &locked, &box, 0))) return;
    const auto &cells = v.grid.cells();
    const std::size_t row = std::size_t(dirty.x1 - dirty.x0 + 1);
    for (int z = dirty.z0; z <= dirty.z1; ++z)
        for (int y = dirty.y0; y <= dirty.y1; ++y)
            std::memcpy(static_cast<BYTE *>(locked.pBits) + std::size_t(z - dirty.z0) * std::size_t(locked.SlicePitch) +
                                std::size_t(y - dirty.y0) * std::size_t(locked.RowPitch),
                        &cells[v.grid.index(dirty.x0, y, z)], row);
    v.texture->UnlockBox(0);
}

}  // namespace

// A thing of the map is going: out of the voxel copy, and of those waiting to get into it.
void voxels_forget(const CMatrixMapStatic *object) {
    if (!g_voxels.map) return;
    if (g_voxels.queued.erase(object)) std::erase(g_voxels.queue, object);
    if (g_timing && g_voxels.grid.has(object))
        lgr.notice("Shaders: voxels: {} gone ({} things left)")(object->IsBuilding() ? "a building"
                                                               : object->IsCannon() ? "a turret"
                                                                                    : "an object",
                                                               g_voxels.grid.things() - 1);
    g_voxels.grid.remove(object);
}

void begin_frame() {
    const auto &choice = ShaderPacks::choice();
    if (!choice.on || choice.pack.empty()) {
        if (g_state.version) {
            release_code();
            release_default();
            g_state.version = 0;
            g_state.problem.clear();
        }
        decide_game_settings(false);
        ShaderLights::set_collecting(false);
        g_voxels.wanted = 0;
        if (g_voxels.map) voxels_drop();
        return;
    }
    if (g_state.version != ShaderPacks::choice_version()) {
        g_state.version = ShaderPacks::choice_version();
        g_state.started = timeGetTime();
        build();
    }
    decide_game_settings(g_state.ready);
    ShaderLights::set_collecting(g_state.ready);
    if (!g_state.ready) return;
    if (g_MatrixMap) update_horizon(*g_MatrixMap);  // (before the frame: its pictures are read, not drawn)
    if (g_MatrixMap) voxels_update(*g_MatrixMap);
    IDirect3DSurface9 *screen = nullptr;
    if (FAILED(g_D3DD->GetRenderTarget(0, &screen))) return;
    D3DSURFACE_DESC desc{};
    screen->GetDesc(&desc);
    if ((desc.Width != g_state.width || desc.Height != g_state.height) && !make_surfaces(desc.Width, desc.Height)) {
        drop(screen);
        return;
    }
    g_state.screen = screen;
    g_D3DD->GetDepthStencilSurface(&g_state.screen_depth);
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
    g_D3DD->SetDepthStencilSurface(g_state.depth_surface);
    g_state.taken = true;
    g_state.solid_taken = false;
    g_state.shadow_drawn = false;
    if (g_timing && !g_timing_state.broken) {
        auto &t = g_timing_state;
        t.now = (t.now + 1) % timing_sets;
        TimingSet &set = t.sets[t.now];
        if (!set.disjoint) {
            bool made = SUCCEEDED(g_D3DD->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &set.disjoint)) &&
                        SUCCEEDED(g_D3DD->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &set.frequency));
            for (int i = 0; made && i < timing_parts; ++i)
                made = SUCCEEDED(g_D3DD->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &set.begin[i])) &&
                       SUCCEEDED(g_D3DD->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &set.end[i]));
            if (!made) {
                timing_drop();
                t.broken = true;
                lgr.warning("Shaders: the video card gives no timestamps: nothing measured");
                return;
            }
        }
        std::fill(std::begin(set.used), std::end(set.used), false);
        set.disjoint->Issue(D3DISSUE_BEGIN);
        set.frequency->Issue(D3DISSUE_END);
        set.issued = true;
        time_begin(Part::Frame);
    }
}

void time_begin(Part part) {
    if (!g_timing || g_timing_state.broken || !g_state.taken) return;
    TimingSet &set = g_timing_state.sets[g_timing_state.now];
    if (!set.issued || !set.begin[int(part)]) return;
    set.begin[int(part)]->Issue(D3DISSUE_END);
}

void time_end(Part part) {
    if (!g_timing || g_timing_state.broken || !g_state.taken) return;
    TimingSet &set = g_timing_state.sets[g_timing_state.now];
    if (!set.issued || !set.end[int(part)]) return;
    set.end[int(part)]->Issue(D3DISSUE_END);
    set.used[int(part)] = true;
}

namespace {
// The frame's last timestamps, then the oldest set's (issued some frames ago) into the sums; every 120 frames the
// averages into test.log.
void timing_frame_end() {
    if (!g_timing || g_timing_state.broken) return;
    auto &t = g_timing_state;
    TimingSet &set = t.sets[t.now];
    if (!set.issued) return;
    time_end(Part::Frame);
    t.water_parts_most = std::max(t.water_parts_most, t.water_parts);
    t.water_parts = 0;
    set.disjoint->Issue(D3DISSUE_END);
    TimingSet &old = t.sets[(t.now + 1) % timing_sets];
    if (!old.issued) return;
    BOOL disjoint = TRUE;
    UINT64 frequency = 0;
    if (old.disjoint->GetData(&disjoint, sizeof(disjoint), 0) != S_OK ||
        old.frequency->GetData(&frequency, sizeof(frequency), 0) != S_OK)
        return;  // (not yet: the next frame reads the next set)
    old.issued = false;
    if (disjoint || frequency == 0) return;
    for (int i = 0; i < timing_parts; ++i) {
        if (!old.used[i]) continue;
        UINT64 a = 0, b = 0;
        if (old.begin[i]->GetData(&a, sizeof(a), 0) != S_OK || old.end[i]->GetData(&b, sizeof(b), 0) != S_OK || b < a)
            continue;
        t.sums[i] += double(b - a) * 1000.0 / double(frequency);
        t.counts[i] += 1;
    }
    if (t.frames == 0) t.started = timeGetTime();
    if (++t.frames < 120) return;
    std::string line;
    for (int i = timing_parts - 1; i >= 0; --i)
        if (t.counts[i]) line += std::format("{} {:.2f}, ", part_name(i), t.sums[i] / t.counts[i]);
    const double wall = double(timeGetTime() - t.started) / double(t.frames - 1);
    lgr.notice("Shaders: the card's ms a frame over {} frames: {}a frame every {:.1f} ms; lights at most {} ({} seen), "
               "{} frames had more than kept; water parts at most {}")(t.frames, line, wall, t.lights_most, t.lights_seen_most,
                                                         t.lights_full, t.water_parts_most);
    t.water_parts_most = 0;
    t.lights_most = t.lights_seen_most = 0;
    t.lights_full = 0;
    std::fill(std::begin(t.sums), std::end(t.sums), 0.0);
    std::fill(std::begin(t.counts), std::end(t.counts), 0);
    t.frames = 0;
}
}  // namespace

// The map light's view over what the camera sees (ShaderShadow::fit), into the shadow map's depth. The engine then
// draws what casts shadows with its own code (the fixed pipeline: only the view and the projection are other).
bool shadow_begin() {
    if (!g_state.taken || !g_state.shadow_surface || !g_MatrixMap) return false;
    const CMatrixMap &map = *g_MatrixMap;
    const auto light = ShaderShadow::normalized({map.m_LightMain.x, map.m_LightMain.y, map.m_LightMain.z});
    if (!(light.z < -0.05f)) return false;  // (a light from under the ground: no shadows to keep)
    const auto &camera = map.m_Camera;
    D3DXMATRIX view_proj = camera.GetViewMatrix() * camera.GetProjMatrix(), inverse;
    if (!D3DXMatrixInverse(&inverse, nullptr, &view_proj)) return false;
    const D3DXVECTOR3 &eye = camera.GetFrustumCenter();
    ShaderShadow::Vec corners[4];
    for (int i = 0; i < 4; ++i) {
        const D3DXVECTOR3 at(i % 2 ? 1.0f : -1.0f, i < 2 ? 1.0f : -1.0f, 1.0f);
        D3DXVECTOR3 far_point;
        D3DXVec3TransformCoord(&far_point, &at, &inverse);
        corners[i] = {far_point.x - eye.x, far_point.y - eye.y, far_point.z - eye.z};
    }
    const float distance = std::min(g_state.pack.shadow.distance, float(MAX_VIEW_DISTANCE));
    const auto fit = ShaderShadow::fit({eye.x, eye.y, eye.z}, corners, WATER_LEVEL, distance, light,
                                       g_state.shadow_size);
    const D3DXMATRIX view(fit.view), proj(fit.proj);
    g_D3DD->GetViewport(&g_state.viewport);
    if (FAILED(g_D3DD->SetRenderTarget(0, g_state.shadow_color)) ||
        FAILED(g_D3DD->SetDepthStencilSurface(g_state.shadow_surface))) {
        shadow_end();
        return false;
    }
    g_D3DD->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    g_D3DD->SetTransform(D3DTS_VIEW, &view);
    g_D3DD->SetTransform(D3DTS_PROJECTION, &proj);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    g_state.shadow_view_proj = view * proj;
    g_state.shadow_fit = fit;
    g_state.shadow_texel = fit.texel;
    g_state.shadow_depth = fit.depth;
    g_state.shadow_drawn = true;
    return true;
}

namespace {

// The vertex shader for a vertex format of the world's drawing: the place, and what of the normal, the colour and the
// texture's coordinates the format has (the others as nothing). The place in the world goes on to the pixel shader.
IDirect3DVertexShader9 *world_vertex(DWORD format) {
    if ((format & D3DFVF_POSITION_MASK) != D3DFVF_XYZ) return nullptr;
    const bool normal = format & D3DFVF_NORMAL, color = format & D3DFVF_DIFFUSE;
    const bool uv = (format & D3DFVF_TEXCOUNT_MASK) != 0;
    const DWORD key = (normal ? 1 : 0) | (color ? 2 : 0) | (uv ? 4 : 0);
    if (const auto found = g_state.world_vertices.find(key); found != g_state.world_vertices.end())
        return found->second;
    const std::string source =
            std::string("row_major float4x4 WorldM : register(c0);\n"
                        "row_major float4x4 ViewProjM : register(c4);\n"
                        "row_major float4x4 WorldBeforeM : register(c8);\n"
                        "row_major float4x4 ViewProjBeforeM : register(c12);\n"
                        "struct In {\n    float4 pos : POSITION;\n") +
            (normal ? "    float3 normal : NORMAL;\n" : "") + (color ? "    float4 color : COLOR0;\n" : "") +
            (uv ? "    float2 uv : TEXCOORD0;\n" : "") +
            "};\n"
            "struct Out {\n"
            "    float4 hpos : POSITION;\n"
            "    float3 pos : TEXCOORD0;\n"
            "    float3 normal : TEXCOORD1;\n"
            "    float2 uv : TEXCOORD2;\n"
            "    float4 now : TEXCOORD3;\n"
            "    float4 before : TEXCOORD4;\n"
            "    float4 color : COLOR0;\n"
            "};\n"
            "Out main(In i) {\n"
            "    Out o;\n"
            "    float4 p = mul(float4(i.pos.xyz, 1), WorldM);\n"
            "    o.hpos = mul(p, ViewProjM);\n"
            "    o.pos = p.xyz;\n"
            "    o.now = o.hpos;\n"
            "    o.before = mul(mul(float4(i.pos.xyz, 1), WorldBeforeM), ViewProjBeforeM);\n"
            "    o.normal = " +
            (normal ? "mul(i.normal, (float3x3)WorldM)" : "float3(0, 0, 0)") + ";\n    o.uv = " +
            (uv ? "i.uv" : "float2(0, 0)") + ";\n    o.color = " + (color ? "i.color" : "float4(1, 1, 1, 1)") +
            ";\n    return o;\n}\n";
    std::wstring problem;
    IDirect3DVertexShader9 *shader = nullptr;
    const auto code = compile(source, "world vertex", "main", "vs_3_0", nullptr, problem);
    if (!code || FAILED(g_D3DD->CreateVertexShader(reinterpret_cast<const DWORD *>(code->data()), &shader)))
        shader = nullptr;
    g_state.world_vertices[key] = shader;
    return shader;
}

// The map's normals at its points into a texture (once a map): the land's mesh keeps none, and each triangle's own
// shows the mesh.
void make_land(CMatrixMap &map) {
    const int width = map.m_Size.x + 1, height = map.m_Size.y + 1;
    if (g_state.land_map == &map && g_state.land_width == width && g_state.land_height == height) return;
    drop(g_state.land);
    g_state.land_map = &map;
    g_state.land_width = width;
    g_state.land_height = height;
    if (width < 2 || height < 2 || DWORD(width) > g_D3DDCaps.MaxTextureWidth ||
        DWORD(height) > g_D3DDCaps.MaxTextureHeight)
        return;
    if (FAILED(g_D3DD->CreateTexture(UINT(width), UINT(height), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_state.land,
                                     nullptr)))
        return;
    D3DLOCKED_RECT locked;
    if (FAILED(g_state.land->LockRect(0, &locked, nullptr, 0))) {
        drop(g_state.land);
        return;
    }
    for (int y = 0; y < height; ++y) {
        DWORD *row = reinterpret_cast<DWORD *>(static_cast<BYTE *>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < width; ++x) {
            D3DXVECTOR3 n = map.PointGet(x, y)->n;
            if (D3DXVec3LengthSq(&n) < 1e-6f) n = D3DXVECTOR3(0, 0, 1);
            D3DXVec3Normalize(&n, &n);
            const auto byte = [](float v) { return DWORD(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            row[x] = 0xff000000u | (byte(n.x) << 16) | (byte(n.y) << 8) | byte(n.z);
        }
    }
    g_state.land->UnlockRect(0);
}

// Before a draw of the world pass (g_WorldDrawHook): the game has set the draw's states and textures for its fixed
// pipeline; the shaders take its place. The game's blending off (an overlay's second drawing would add to or
// multiply what the pack wrote); a clip plane of the game's (a robot's chassis by halves) into the shader's space,
// where a vertex shader's planes are.
void world_hook(bool visible) {
    DWORD format = 0;
    g_D3DD->GetFVF(&format);
    IDirect3DVertexShader9 *vertex = world_vertex(format);
    g_D3DD->SetVertexShader(vertex);
    g_D3DD->SetPixelShader(vertex ? g_state.world : nullptr);
    D3DXMATRIX world;
    g_D3DD->GetTransform(D3DTS_WORLD, &world);
    g_D3DD->SetVertexShaderConstantF(0, &world._11, 4);
    // Where this draw's vertices were a frame ago: the same draw of the same thing then (a thing not drawn then,
    // and the land: where they are).
    const D3DXMATRIX *before = &world;
    if (g_state.world_who) {
        const std::pair<const void *, int> key{g_state.world_who, g_state.world_index++};
        if (const auto found = g_state.world_before.find(key); found != g_state.world_before.end())
            before = &found->second;
        g_state.world_now[key] = world;
    }
    g_D3DD->SetVertexShaderConstantF(8, &before->_11, 4);
    g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    // (a draw for the depth alone, or one in a format unknown here: nothing into the further targets; the game
    // itself keeps such a draw out of the first)
    // (the game sets none of the further targets' own: set again only when they change)
    const DWORD write = visible && vertex ? 0xf : 0;
    if (write != g_state.world_write) {
        g_state.world_write = write;
        g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE1, write);
        g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE2, write);
        g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE3, write);
    }
    DWORD planes = 0;
    g_D3DD->GetRenderState(D3DRS_CLIPPLANEENABLE, &planes);
    if (planes & D3DCLIPPLANE0) {
        float plane[4]{};
        g_D3DD->GetClipPlane(0, plane);
        if (!g_state.world_plane_set || std::memcmp(plane, g_state.world_plane, sizeof(plane)) != 0) {
            D3DXMATRIX into;
            if (D3DXMatrixInverse(&into, nullptr, &g_state.world_view_proj)) {
                D3DXMatrixTranspose(&into, &into);
                const D3DXPLANE from(plane[0], plane[1], plane[2], plane[3]);
                D3DXPLANE moved;
                D3DXPlaneTransform(&moved, &from, &into);
                std::memcpy(g_state.world_plane, &moved.a, sizeof(plane));
                g_D3DD->SetClipPlane(0, g_state.world_plane);
                g_state.world_plane_set = true;
            }
        }
    }
}

}  // namespace

bool world_begin() {
    if (!g_state.taken || !g_state.world || !g_state.world_depth || !g_MatrixMap) return false;
    const CMatrixMap &map = *g_MatrixMap;
    g_D3DD->GetViewport(&g_state.viewport);
    for (std::size_t i = 0; i < g_state.world_outputs.size(); ++i)
        if (FAILED(g_D3DD->SetRenderTarget(DWORD(i), g_state.target_surfaces[g_state.world_outputs[i]]))) {
            world_end();
            return false;
        }
    g_D3DD->SetDepthStencilSurface(g_state.world_depth);
    g_D3DD->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    // The constants a world shader has (a pass's, in its registers; the rest wait for the finished picture).
    const auto set = [](int reg, const D3DXVECTOR4 &value) { g_D3DD->SetPixelShaderConstantF(reg, &value.x, 1); };
    const auto &camera = map.m_Camera;
    const D3DXMATRIX &view = camera.GetViewMatrix(), &proj = camera.GetProjMatrix();
    g_state.world_view_proj = view * proj;
    const D3DXVECTOR3 &eye = camera.GetFrustumCenter();
    set(2, {float(timeGetTime() - g_state.started) / 1000.0f, float(map.GetTime()) / 1000.0f, g_state.frame, 0});
    set(3, {eye.x, eye.y, eye.z, 1});
    set(4, {-map.m_LightMain.x, -map.m_LightMain.y, -map.m_LightMain.z, 0});
    set(5, color(map.m_LightMainColor, 1));
    set(6, color(map.m_AmbientColor, 1));
    set(11, {WATER_LEVEL, float(map.m_Size.x) * GLOBAL_SCALE, float(map.m_Size.y) * GLOBAL_SCALE, 0});
    g_D3DD->SetPixelShaderConstantF(12, &view._11, 4);
    g_D3DD->SetPixelShaderConstantF(16, &proj._11, 4);
    g_D3DD->SetPixelShaderConstantF(20, &g_state.world_view_proj._11, 4);
    g_D3DD->SetVertexShaderConstantF(4, &g_state.world_view_proj._11, 4);
    // The previous frame's camera (finish_scene keeps it) and things' places.
    const D3DXMATRIX &view_proj_before = g_state.has_previous ? g_state.previous_view_proj : g_state.world_view_proj;
    g_D3DD->SetVertexShaderConstantF(12, &view_proj_before._11, 4);
    g_state.world_before.swap(g_state.world_now);
    g_state.world_now.clear();
    // The land's normals (a sampler the game's drawing does not use), a point's place in them.
    make_land(*g_MatrixMap);
    if (g_state.land) {
        sampler(8, g_state.land, true, false);
        set(138, {1.0f / GLOBAL_SCALE, 0, 1.0f / float(g_state.land_width), 1.0f / float(g_state.land_height)});
    }
    world_material(0, nullptr);
    g_state.world_plane_set = false;
    g_state.world_write = 0x100;  // (none set yet)
    g_WorldDrawHook = world_hook;
    return true;
}

void world_material(int material, const void *who) {
    g_state.world_who = who;
    g_state.world_index = 0;
    const D3DXVECTOR4 info{float(material), g_state.land ? 1.0f : 0.0f, 0, 0};
    g_D3DD->SetPixelShaderConstantF(137, &info.x, 1);
}

// The game's fixed pipeline and the picture again.
void world_end() {
    g_WorldDrawHook = nullptr;
    g_D3DD->SetVertexShader(nullptr);
    g_D3DD->SetPixelShader(nullptr);
    for (DWORD i = 1; i < DWORD(ShaderPacks::max_world_outputs); ++i) g_D3DD->SetRenderTarget(i, nullptr);
    g_D3DD->SetTexture(8, nullptr);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xf);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE2, 0xf);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE3, 0xf);
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
    g_D3DD->SetDepthStencilSurface(g_state.depth_surface);
    g_D3DD->SetViewport(&g_state.viewport);
}

bool shadow_keeps(float x, float y, float z, float radius) {
    return ShaderShadow::covers(g_state.shadow_fit, {x, y, z}, radius);
}

bool world_keeps(float x, float y, float z, float radius) {
    if (!g_MatrixMap) return true;
    // Its radius in the screen's pixels at least this: smaller, the lights' half size pass reads a pixel or two of it.
    constexpr float least = 4.0f;
    const auto &camera = g_MatrixMap->m_Camera;
    const D3DXMATRIX &view = camera.GetViewMatrix(), &proj = camera.GetProjMatrix();
    const float depth = x * view._13 + y * view._23 + z * view._33 + view._43;
    if (depth <= radius) return true;
    // (its radius in the screen's pixels)
    return radius / depth * proj._22 * float(g_state.viewport.Height) * 0.5f >= least;
}

// The picture is the target again (its view and projection: the caller's, set next).
void shadow_end() {
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
    g_D3DD->SetDepthStencilSurface(g_state.depth_surface);
    g_D3DD->SetViewport(&g_state.viewport);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
}

namespace {

// The depth so far into depth_copy (the picture and its depth the targets again after it). The caller keeps the
// states (a state block): this changes them.
bool copy_depth() {
    if (!g_state.depth || !g_state.depth_copy_surface || !g_state.copy) return false;
    g_D3DD->SetDepthStencilSurface(nullptr);
    g_D3DD->SetRenderTarget(0, g_state.depth_copy_surface);
    for (const auto &[state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
             {D3DRS_ZENABLE, D3DZB_FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE},
             {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_FOGENABLE, FALSE},
             {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE},
             {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FILLMODE, D3DFILL_SOLID}, {D3DRS_CLIPPLANEENABLE, 0}})
        g_D3DD->SetRenderState(state, value);
    g_D3DD->SetVertexShader(g_state.vertex);
    g_D3DD->SetPixelShader(g_state.copy);
    g_D3DD->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
    const D3DXVECTOR4 half{1.0f / float(g_state.width), 1.0f / float(g_state.height), 0, 0};
    g_D3DD->SetVertexShaderConstantF(0, &half.x, 1);
    sampler(0, g_state.depth, true, true);
    g_D3DD->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(Vertex));
    g_D3DD->SetTexture(0, nullptr);
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
    g_D3DD->SetDepthStencilSurface(g_state.depth_surface);
    return true;
}

// The constants the water's and the effects' code share with a pass's (ShaderPacks' view_constants).
void view_constants(const CMatrixMap &map) {
    const auto set = [](int reg, const D3DXVECTOR4 &value) { g_D3DD->SetPixelShaderConstantF(reg, &value.x, 1); };
    const auto &camera = map.m_Camera;
    const D3DXMATRIX &view = camera.GetViewMatrix(), &proj = camera.GetProjMatrix();
    D3DXMATRIX view_proj = view * proj, inverse;
    if (!D3DXMatrixInverse(&inverse, nullptr, &view_proj)) D3DXMatrixIdentity(&inverse);
    const float w = float(g_state.width), h = float(g_state.height);
    set(1, {w, h, 1.0f / w, 1.0f / h});
    set(2, {float(timeGetTime() - g_state.started) / 1000.0f, float(map.GetTime()) / 1000.0f, g_state.frame, 0});
    const D3DXVECTOR3 &eye = camera.GetFrustumCenter();
    set(3, {eye.x, eye.y, eye.z, 1});
    set(4, {-map.m_LightMain.x, -map.m_LightMain.y, -map.m_LightMain.z, 0});
    set(5, color(map.m_LightMainColor, 1));
    set(6, color(map.m_AmbientColor, 1));
    set(7, color(map.m_SkyColor, FLAG(map.m_Flags, MMFLAG_FOG_ON) && g_game_fog ? 1.0f : 0.0f));
    set(8, {float(MAX_VIEW_DISTANCE * FOG_NEAR_K), float(MAX_VIEW_DISTANCE * FOG_FAR_K), MAX_VIEW_DISTANCE, 0});
    const float near_plane = proj._33 != 0 ? -proj._43 / proj._33 : 1.0f;
    const float far_plane = proj._33 != 1 ? proj._43 / (1.0f - proj._33) : MAX_VIEW_DISTANCE;
    set(9, {proj._33, proj._43, near_plane, far_plane});
    set(11, {WATER_LEVEL, float(map.m_Size.x) * GLOBAL_SCALE, float(map.m_Size.y) * GLOBAL_SCALE, 0});
    g_D3DD->SetPixelShaderConstantF(12, &view._11, 4);
    g_D3DD->SetPixelShaderConstantF(16, &proj._11, 4);
    g_D3DD->SetPixelShaderConstantF(20, &view_proj._11, 4);
    g_D3DD->SetPixelShaderConstantF(24, &inverse._11, 4);
}

D3DXVECTOR4 times(const D3DCOLORVALUE &a, const D3DCOLORVALUE &b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

}  // namespace

bool water_begin(IDirect3DTexture9 *texture, IDirect3DTexture9 *reflection) {
    if (!g_state.taken || !g_state.water || !g_state.water_vertex || !g_MatrixMap) return false;
    const CMatrixMap &map = *g_MatrixMap;
    g_state.saved->Capture();
    // The picture and the depth before the water.
    if (g_state.under_surface)
        g_D3DD->StretchRect(g_state.scene_surface, nullptr, g_state.under_surface, nullptr, D3DTEXF_NONE);
    const bool depth = copy_depth();
    g_state.saved->Apply();
    // The game's light on the water, as its fixed pipeline would light it: the water's colour for the ambient, the
    // first light if it is a directional one, the material.
    D3DMATERIAL9 material{};
    g_D3DD->GetMaterial(&material);
    D3DLIGHT9 light{};
    BOOL on = FALSE;
    g_D3DD->GetLight(0, &light);
    g_D3DD->GetLightEnable(0, &on);
    const bool directional = on && light.Type == D3DLIGHT_DIRECTIONAL;
    D3DCOLORVALUE water{};
    {
        const D3DXVECTOR4 c = color(map.m_WaterColor, 1);
        water = {c.x, c.y, c.z, 1};
    }
    D3DXVECTOR4 ambient = times(water, material.Ambient);
    if (on) ambient += times(light.Ambient, material.Ambient);
    ambient += D3DXVECTOR4(material.Emissive.r, material.Emissive.g, material.Emissive.b, 0);
    const D3DXVECTOR4 diffuse = directional ? times(light.Diffuse, material.Diffuse) : D3DXVECTOR4(0, 0, 0, 0);
    D3DXVECTOR3 towards(-light.Direction.x, -light.Direction.y, -light.Direction.z);
    if (D3DXVec3LengthSq(&towards) > 1e-12f) D3DXVec3Normalize(&towards, &towards);
    DWORD normalize = FALSE;
    g_D3DD->GetRenderState(D3DRS_NORMALIZENORMALS, &normalize);
    g_state.water_light = {towards.x, towards.y, towards.z, normalize ? 0.0f : 1.0f};
    // The game's fog (its table's: by the depth where the card can, else by the depth buffer's value).
    DWORD fog = FALSE, start = 0, end = 0;
    g_D3DD->GetRenderState(D3DRS_FOGENABLE, &fog);
    g_D3DD->GetRenderState(D3DRS_FOGSTART, &start);
    g_D3DD->GetRenderState(D3DRS_FOGEND, &end);
    float fog_start = 0, fog_end = 0;
    std::memcpy(&fog_start, &start, sizeof(float));
    std::memcpy(&fog_end, &end, sizeof(float));
    const auto &camera = map.m_Camera;
    const D3DXMATRIX &view = camera.GetViewMatrix();
    const D3DXMATRIX view_proj = view * camera.GetProjMatrix();
    g_D3DD->SetVertexShaderConstantF(4, &view_proj._11, 4);
    g_D3DD->SetVertexShaderConstantF(8, &view._11, 4);
    g_D3DD->SetVertexShaderConstantF(13, &diffuse.x, 1);
    g_D3DD->SetVertexShaderConstantF(14, &ambient.x, 1);
    const D3DXVECTOR4 fog_info{fog_start, fog_end, fog ? 1.0f : 0.0f,
                               (g_D3DDCaps.RasterCaps & D3DPRASTERCAPS_WFOG) ? 1.0f : 0.0f};
    g_D3DD->SetVertexShaderConstantF(15, &fog_info.x, 1);
    view_constants(map);
    // (the fog is the shader's: shader model 3 has none of its own)
    g_D3DD->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_D3DD->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_D3DD->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_D3DD->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    // The game's own textures read as it reads them (wrapped; their filter and bias as DrawWater set them).
    for (const auto &[index, made] : {std::pair{1, texture}, {2, reflection}}) {
        g_D3DD->SetTexture(index, made);
        g_D3DD->SetSamplerState(index, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        g_D3DD->SetSamplerState(index, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        g_D3DD->SetSamplerState(index, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_D3DD->SetSamplerState(index, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_D3DD->SetSamplerState(index, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    }
    sampler(3, g_state.under ? static_cast<IDirect3DBaseTexture9 *>(g_state.under) : g_state.white, true, false);
    sampler(4, depth ? static_cast<IDirect3DBaseTexture9 *>(g_state.depth_copy) : g_state.white, true, true);
    g_D3DD->SetVertexShader(g_state.water_vertex);
    g_D3DD->SetPixelShader(g_state.water);
    return true;
}

void water_part(IDirect3DTexture9 *mask, const _D3DMATRIX &world) {
    ++g_timing_state.water_parts;
    sampler(0, mask ? static_cast<IDirect3DBaseTexture9 *>(mask) : g_state.white, true, false);
    g_D3DD->SetVertexShaderConstantF(0, &world._11, 4);
    // The normal as the game's lighting has it: scaled by the inverse of the world's scale, unless normalized.
    const float scale = std::sqrt(world._11 * world._11 + world._12 * world._12 + world._13 * world._13);
    D3DXVECTOR4 light = g_state.water_light;
    light.w = light.w > 0 && scale > 1e-6f ? 1.0f / scale : 0.0f;
    g_D3DD->SetVertexShaderConstantF(12, &light.x, 1);
}

void water_end() {
    g_state.saved->Apply();
    g_D3DD->SetVertexShader(nullptr);
    g_D3DD->SetPixelShader(nullptr);
}

namespace {

// Before an effect's draw: the pack's shader for its stages (built the first time such stages come), the factor and
// how the game lays it; the game's own drawing for one the code cannot do, or one with a vertex shader of its own.
// facing: the effect is turned to the camera (sprites, smoke, fire, beams, booms); false: laid in the world (the light
// spots on the land, the capture spots, the rings on the ground and the water).
void effect_hook(bool facing) {
    IDirect3DVertexShader9 *vertex = nullptr;
    g_D3DD->GetVertexShader(&vertex);
    if (vertex) {
        vertex->Release();
        g_D3DD->SetPixelShader(nullptr);
        return;
    }
    ShaderPacks::Stages stages{};
    std::string key;
    bool plain = true;  // nothing the code cannot do (a stage's result into a temporary, a fourth stage)
    for (int s = 0; s < ShaderPacks::effect_stages; ++s) {
        DWORD v[7]{};
        g_D3DD->GetTextureStageState(s, D3DTSS_COLOROP, &v[0]);
        g_D3DD->GetTextureStageState(s, D3DTSS_COLORARG1, &v[1]);
        g_D3DD->GetTextureStageState(s, D3DTSS_COLORARG2, &v[2]);
        g_D3DD->GetTextureStageState(s, D3DTSS_ALPHAOP, &v[3]);
        g_D3DD->GetTextureStageState(s, D3DTSS_ALPHAARG1, &v[4]);
        g_D3DD->GetTextureStageState(s, D3DTSS_ALPHAARG2, &v[5]);
        g_D3DD->GetTextureStageState(s, D3DTSS_RESULTARG, &v[6]);
        IDirect3DBaseTexture9 *texture = nullptr;
        g_D3DD->GetTexture(s, &texture);
        const bool cube = texture && texture->GetType() == D3DRTYPE_CUBETEXTURE;
        if (texture) texture->Release();
        stages[s] = {v[0], v[1], v[2], v[3], v[4], v[5], cube};
        if (v[0] == D3DTOP_DISABLE) break;
        plain = plain && v[6] == D3DTA_CURRENT;
        if (s == ShaderPacks::effect_stages - 1) {
            DWORD next = D3DTOP_DISABLE;
            g_D3DD->GetTextureStageState(s + 1, D3DTSS_COLOROP, &next);
            plain = plain && next == D3DTOP_DISABLE;
        }
        for (DWORD value : v) key += std::to_string(value) + ",";
        key += cube ? "c;" : ";";
    }
    IDirect3DPixelShader9 *shader = nullptr;
    if (plain) {
        // A kind seen before; the one compiled beside the drawing, when it is done; else its compiling begun (the
        // compiler takes milliseconds: in the drawing they would be a jerk the first time an explosion comes).
        if (const auto found = g_state.effects.find(key); found != g_state.effects.end()) shader = found->second;
        else {
            auto &build = g_state.effects_build;
            if (build.valid() && build.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto [bytes, words] = build.get();
                IDirect3DPixelShader9 *made = nullptr;
                if (!bytes.empty()) g_D3DD->CreatePixelShader(reinterpret_cast<const DWORD *>(bytes.data()), &made);
                if (!made)
                    lgr.warning("Shaders: effects of stages {}drawn as the game draws them: {}")(g_state.effects_key,
                                                                                               words);
                else if (g_timing)
                    lgr.notice("Shaders: a kind of effects built in {:.1f} ms beside the drawing")(
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                      g_state.effects_started)
                                    .count());
                g_state.effects.emplace(g_state.effects_key, made);
                if (g_state.effects_key == key) shader = made;
            }
            if (!shader && !build.valid() && !g_state.effects.contains(key)) {
                const auto combine = ShaderPacks::combiner(stages);
                if (!combine || !g_state.files) {
                    lgr.warning("Shaders: effects of stages {}drawn as the game draws them")(key);
                    g_state.effects.emplace(key, nullptr);
                }
                else {
                    g_state.effects_key = key;
                    g_state.effects_started = std::chrono::steady_clock::now();
                    build = std::async(std::launch::async,
                                       [source = g_state.effects_source + ShaderPacks::effects_wrapper(stages, *combine),
                                        files = g_state.files, profile = g_state.effects_profile] {
                                           Include include(*files);
                                           ID3DXBuffer *code = nullptr, *messages = nullptr;
                                           const HRESULT result = D3DXCompileShader(
                                                   source.data(), UINT(source.size()), nullptr, &include,
                                                   ShaderPacks::effects_entry, profile, 0, &code, &messages, nullptr);
                                           std::pair<std::string, std::string> out;
                                           if (SUCCEEDED(result) && code)
                                               out.first.assign(static_cast<const char *>(code->GetBufferPointer()),
                                                                code->GetBufferSize());
                                           if (messages)
                                               out.second.assign(static_cast<const char *>(messages->GetBufferPointer()),
                                                                 messages->GetBufferSize());
                                           drop(code);
                                           drop(messages);
                                           return out;
                                       });
                }
            }
        }
    }
    if (shader) {
        DWORD format = 0, factor = 0, blend = FALSE, source = 0, dest = 0;
        g_D3DD->GetFVF(&format);
        g_D3DD->GetRenderState(D3DRS_TEXTUREFACTOR, &factor);
        g_D3DD->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
        g_D3DD->GetRenderState(D3DRS_SRCBLEND, &source);
        g_D3DD->GetRenderState(D3DRS_DESTBLEND, &dest);
        const D3DXVECTOR4 tfactor = color(factor, float((factor >> 24) & 0xff) / 255.0f);
        g_D3DD->SetPixelShaderConstantF(0, &tfactor.x, 1);
        // What Faded scales: the alpha where the game lays it by its alpha, the colour (and the alpha) where it adds
        // it as it is; nothing where it lays it otherwise (multiplied, or not blended).
        const bool by_alpha = blend && source == D3DBLEND_SRCALPHA;
        const bool as_is = blend && source == D3DBLEND_ONE && (dest == D3DBLEND_ONE || dest == D3DBLEND_INVSRCALPHA);
        const bool placed = format != 0 && (format & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW;
        const D3DXVECTOR4 info{placed ? 1.0f : 0.0f, as_is ? 1.0f : 0.0f, (by_alpha || as_is) ? 1.0f : 0.0f,
                               facing ? 1.0f : 0.0f};
        g_D3DD->SetPixelShaderConstantF(10, &info.x, 1);
    }
    g_D3DD->SetPixelShader(shader);
}

}  // namespace

void effects_begin() {
    if (!g_state.taken || !g_state.effects_profile || !g_MatrixMap) return;
    g_state.saved->Capture();
    const bool depth = copy_depth();
    g_state.saved->Apply();
    view_constants(*g_MatrixMap);
    sampler(14, depth ? static_cast<IDirect3DBaseTexture9 *>(g_state.depth_copy) : g_state.white, true, true);
    sampler(15, g_state.solid_taken ? static_cast<IDirect3DBaseTexture9 *>(g_state.solid) : g_state.white, true, false);
    // Where on the screen a pixel of an effect is: the camera's projection of its place in the camera's space, as the
    // eighth stage's coordinates (no stage of the game's uses it).
    const D3DXMATRIX &proj = g_MatrixMap->m_Camera.GetProjMatrix();
    g_D3DD->SetTransform(D3DTS_TEXTURE7, &proj);
    g_D3DD->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION | 7);
    g_D3DD->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT4);
    g_state.effects_drawing = true;
    g_WorldDrawHook = effect_hook;
}

void effects_end() {
    if (!g_state.effects_drawing) return;
    g_state.effects_drawing = false;
    g_WorldDrawHook = nullptr;
    g_D3DD->SetPixelShader(nullptr);
    g_D3DD->SetTexture(14, nullptr);
    g_D3DD->SetTexture(15, nullptr);
    g_D3DD->SetTextureStageState(7, D3DTSS_TEXCOORDINDEX, 7);
    g_D3DD->SetTextureStageState(7, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

// SolidTex's alpha: 0 where the game's volumetric shadows fell, 1 elsewhere. They were just drawn by the stencil
// (CMatrixMap::DrawShadows: a quad of ShadowColor where it is 1 or more), which still holds them: the same test marks
// them, so a pack can take them out and lay them again after its own light (a night's fog veiled them).
void mark_shadows() {
    struct Corner {
        float x, y, z, rhw;
        D3DCOLOR color;
    };
    const float w = float(g_state.width), h = float(g_state.height);
    const auto quad = [&](D3DCOLOR color) {
        const Corner corners[4] = {{-0.5f, h - 0.5f, 0, 1, color}, {-0.5f, -0.5f, 0, 1, color},
                                   {w - 0.5f, h - 0.5f, 0, 1, color}, {w - 0.5f, -0.5f, 0, 1, color}};
        g_D3DD->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, corners, sizeof(Corner));
    };
    g_state.saved->Capture();
    g_D3DD->SetRenderTarget(0, g_state.solid_surface);
    for (const auto &[state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
             {D3DRS_ZENABLE, D3DZB_FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE},
             {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_FOGENABLE, FALSE},
             {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA},
             {D3DRS_FILLMODE, D3DFILL_SOLID}, {D3DRS_TWOSIDEDSTENCILMODE, FALSE}, {D3DRS_STENCILENABLE, FALSE}})
        g_D3DD->SetRenderState(state, value);
    g_D3DD->SetVertexShader(nullptr);
    g_D3DD->SetPixelShader(nullptr);
    g_D3DD->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_D3DD->SetTexture(0, nullptr);
    g_D3DD->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_D3DD->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_D3DD->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_D3DD->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_D3DD->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_D3DD->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    quad(0xffffffff);
    if (FLAG(g_Flags, GFLAG_STENCILAVAILABLE)) {
        for (const auto &[state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
                 {D3DRS_STENCILENABLE, TRUE}, {D3DRS_STENCILFUNC, D3DCMP_LESSEQUAL}, {D3DRS_STENCILREF, 1},
                 {D3DRS_STENCILMASK, 0xffffffff}, {D3DRS_STENCILPASS, D3DSTENCILOP_KEEP},
                 {D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP}, {D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP}})
            g_D3DD->SetRenderState(state, value);
        quad(0x00000000);
    }
    g_state.saved->Apply();
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
}

void before_effects() {
    if (!g_state.taken) return;
    g_state.solid_taken = SUCCEEDED(
            g_D3DD->StretchRect(g_state.scene_surface, nullptr, g_state.solid_surface, nullptr, D3DTEXF_NONE));
    if (g_state.solid_taken) mark_shadows();
}

bool solid_taken() { return g_state.taken && g_state.solid_taken; }

// (Colour only: SolidTex's alpha keeps the shadows.)
bool solid_target() {
    if (!solid_taken() || FAILED(g_D3DD->SetRenderTarget(0, g_state.solid_surface))) return false;
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                                                          D3DCOLORWRITEENABLE_BLUE);
    return true;
}

void scene_target() {
    if (!g_state.taken) return;
    g_D3DD->SetRenderTarget(0, g_state.scene_surface);
    g_D3DD->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
}

void finish_scene() {
    if (!g_state.taken) return;
    g_state.saved->Capture();
    // The engine's 3D states off: the quad is drawn as it is, every pixel once.
    g_D3DD->SetDepthStencilSurface(nullptr);
    for (const auto &[state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
             {D3DRS_ZENABLE, D3DZB_FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE},
             {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_FOGENABLE, FALSE},
             {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE},
             {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FILLMODE, D3DFILL_SOLID}, {D3DRS_CLIPPING, TRUE},
             {D3DRS_MULTISAMPLEANTIALIAS, FALSE}})
        g_D3DD->SetRenderState(state, value);
    time_begin(Part::Passes);
    g_D3DD->SetVertexShader(g_state.vertex);
    g_D3DD->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
    constants(*g_MatrixMap);
    sampler(0, g_state.scene, true, false);
    sampler(1, g_state.depth ? static_cast<IDirect3DBaseTexture9 *>(g_state.depth) : g_state.white, true, true);
    // Without before_effects (a frame of no effects' drawing): the picture as it is.
    sampler(solid_sampler, g_state.solid_taken ? g_state.solid : g_state.scene, true, false);
    IDirect3DTexture9 *previous = g_state.scene;
    int bound = 0;  // the pack's pictures the pass before left in the samplers
    for (const auto &pass : g_state.passes) {
        IDirect3DSurface9 *output = pass.output < 0 ? g_state.screen : g_state.target_surfaces[pass.output];
        g_D3DD->SetRenderTarget(0, output);
        D3DSURFACE_DESC desc{};
        output->GetDesc(&desc);
        const D3DXVECTOR4 size{float(desc.Width), float(desc.Height), 1.0f / float(desc.Width), 1.0f / float(desc.Height)};
        g_D3DD->SetPixelShaderConstantF(0, &size.x, 1);
        const D3DXVECTOR4 half{size.z, size.w, 0, 0};
        g_D3DD->SetVertexShaderConstantF(0, &half.x, 1);
        // A texture is not read while it is drawn into.
        sampler(2, previous == (pass.output < 0 ? nullptr : g_state.targets[pass.output]) ? nullptr : previous, true, false);
        // The pictures its code reads; what the pass before had further on is taken out (a target still in a
        // sampler could be the one drawn into now).
        int slot = picture_sampler;
        for (int index : pass.inputs.targets)
            sampler(slot++, index == pass.output ? nullptr : g_state.targets[index], true, false);
        for (int index : pass.inputs.textures)
            sampler(slot++, g_state.textures[index], g_state.pack.textures[index].clamp,
                    g_state.pack.textures[index].point);
        // (no map this frame: all far, and ShadowInfo.w says so)
        if (pass.inputs.shadow)
            sampler(slot++, g_state.shadow_drawn ? static_cast<IDirect3DBaseTexture9 *>(g_state.shadow) : g_state.white,
                    true, true);
        if (pass.inputs.voxels) sampler(slot++, g_voxels.texture, true, true);  // (none: VoxelScale.w says so)
        const int now = slot - picture_sampler;
        for (; slot < picture_sampler + bound; ++slot) g_D3DD->SetTexture(slot, nullptr);
        bound = now;
        g_D3DD->SetPixelShader(pass.shader);
        g_D3DD->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(), sizeof(Vertex));
        if (pass.output >= 0) previous = g_state.targets[pass.output];
    }
    time_end(Part::Passes);
    g_D3DD->SetRenderTarget(0, g_state.screen);
    g_D3DD->SetDepthStencilSurface(g_state.screen_depth);
    g_D3DD->Clear(0, nullptr, D3DCLEAR_ZBUFFER | (FLAG(g_Flags, GFLAG_STENCILAVAILABLE) ? D3DCLEAR_STENCIL : 0), 0,
                  1.0f, 0);
    g_state.saved->Apply();
    g_D3DD->SetVertexShader(nullptr);
    g_D3DD->SetPixelShader(nullptr);
    g_state.frame += 1;
    g_state.history += 1;
    timing_frame_end();
    release_frame_surfaces();
}

void release() {
    release_default();
    ShaderLights::clear();
}

void shutdown() {
    release_code();
    release_default();
    g_voxels.wanted = 0;
    voxels_drop();
    g_state.version = 0;
    decide_game_settings(false);
}

bool game_fog() { return g_game_fog; }

const std::wstring &problem() { return g_state.problem; }

}  // namespace ShaderRender
