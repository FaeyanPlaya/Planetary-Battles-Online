// MatrixGame - licensed under GPLv2 or any later version.
#include "Shooter.hpp"

#include "MatrixMap.hpp"
#include "MatrixSide.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectCannon.hpp"
#include "ModHooks.hpp"
#include "Mods.hpp"
#include "ShaderPacks.hpp"
#include "StringConstants.hpp"
#include "WindowDisplay.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace Shooter {
namespace {

bool g_held = false;     // the Windows cursor held in the window by us (ClipCursor)
bool g_centred = false;  // the cursor taken to the middle since captured: a move from there turns the view
float g_way = 0;         // the way in, 0..1
constexpr float way_ms = 400.0f;  // as the camera's way into the robot (FIRST_PERSON_BLEND_MS)
constexpr DWORD marker_color = 0xC0FFFFFF, marker_shade = 0x90000000;
constexpr float marker_radius = 10.0f;
constexpr float marker_ms = 70.0f;  // the ring follows where the hull looks this smoothly (the time to 63% of the way)

// The ring's place in the world, smoothed (the hull turns in steps, a shared battle's aim comes a turn at a time).
struct Marker {
    bool shown = false;
    D3DXVECTOR3 at{0, 0, 0};
};
Marker g_marker;

// The window's middle: in its client area (Windows' cursor) and in the game's screen (the game's cursor; the same
// mapping as the mouse's moves, so the cursor put there comes back as no move at all).
struct Middle {
    POINT client;
    int x, y;
};
Middle middle() {
    RECT rect{};
    if (!g_Wnd || !GetClientRect(g_Wnd, &rect) || rect.right <= 0 || rect.bottom <= 0)
        return {{g_ScreenX / 2, g_ScreenY / 2}, g_ScreenX / 2, g_ScreenY / 2};
    Middle m{{rect.right / 2, rect.bottom / 2}, rect.right / 2, rect.bottom / 2};
    Display::map_mouse(WM_MOUSEMOVE, m.x, m.y);
    return m;
}

void to_middle(const Middle &m) {
    if (g_Wnd) {
        POINT at = m.client;
        if (ClientToScreen(g_Wnd, &at)) SetCursorPos(at.x, at.y);
    }
    g_MatrixMap->m_Cursor.SetPos(m.x, m.y);
}

void hold(bool on) {
    if (on && g_Wnd) {
        RECT rect{};
        GetClientRect(g_Wnd, &rect);
        POINT from{rect.left, rect.top}, to{rect.right, rect.bottom};
        ClientToScreen(g_Wnd, &from);
        ClientToScreen(g_Wnd, &to);
        const RECT clip{from.x, from.y, to.x, to.y};
        ClipCursor(&clip);
        g_held = true;
    }
    else if (g_held) {
        ClipCursor(nullptr);
        g_held = false;
    }
}

// Where the hull (the turret's head) looks now, as far along as the aim and as high: the ring's place (a robot's
// weapons fire there).
bool marker_target(D3DXVECTOR3 &at) {
    CMatrixSideUnit *side = g_MatrixMap->GetPlayerSide();
    CMatrixMapStatic *unit = side->GetArcadedObject();
    D3DXVECTOR3 from, look;
    if (unit->IsRobot()) {
        at = hull_aim(*unit->AsRobot(), side->DriveAim());
        return true;
    }
    if (unit->IsCannon()) {
        from = unit->GetGeoCenter();
        from.z = 0;
        const float angle = unit->AsCannon()->HeadAngle() + float(M_PI_MUL(1));  // as the camera's (CalcLinkPoint)
        look = D3DXVECTOR3(std::sin(angle), -std::cos(angle), 0);
    }
    else
        return false;
    if (D3DXVec3LengthSq(&look) < 1e-6f) return false;
    D3DXVec3Normalize(&look, &look);
    const D3DXVECTOR3 aim = side->DriveAim();
    const float reach = std::max(std::hypot(aim.x - from.x, aim.y - from.y), 20.0f);
    at = from + look * reach;
    at.z = aim.z;
    return true;
}

void begin_lines() {
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA));
    g_D3DD->SetPixelShader(nullptr);
    g_D3DD->SetVertexShader(nullptr);
    SetColorOpSelect(0, D3DTA_TFACTOR);
    SetAlphaOpSelect(0, D3DTA_TFACTOR);
    SetColorOpDisable(1);
    ASSERT_DX(g_D3DD->SetFVF(D3DFVF_XYZRHW));
}

void end_lines() {
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
    ASSERT_DX(g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
}

}  // namespace

bool on() {
    const auto &hooks = ModHooks::hooks();
    return Mods::switches().shooter && Mods::switches().manual_extras && hooks.shooter_turn && hooks.shooter_keys &&
           hooks.shooter_look;
}

bool driving() {
    if (!on() || !g_MatrixMap || g_MatrixMap->IsSpectating()) return false;
    CMatrixSideUnit *side = g_MatrixMap->GetPlayerSide();
    return side && side->GetArcadedObject() && (side->IsRobotMode() || side->IsTurretMode());
}

bool captured() {
    return driving() && !g_MatrixMap->IsPaused() && !FLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE) &&
           (!g_Wnd || GetForegroundWindow() == g_Wnd);
}

bool mouse_move(int x, int y) {
    if (!captured()) {
        g_centred = false;
        return false;
    }
    const Middle m = middle();
    if (g_centred && (x != m.x || y != m.y)) {
        const float k = ModHooks::hooks().shooter_look();
        g_MatrixMap->m_Camera.ShooterLook(float(x - m.x) * k, float(y - m.y) * k);
    }
    g_centred = true;
    to_middle(m);
    return true;
}

void frame(int ms) {
    const bool now = captured();
    hold(now);
    // The game's cursor drawn by the game in the middle while captured (Windows drew it where the mouse was between
    // the moves put back to the middle: it shook).
    g_MatrixMap->m_Cursor.SetPinned(now);
    if (now && !g_centred) {
        to_middle(middle());  // just in (or back): from the middle on, not a turn by where the cursor was
        g_centred = true;
    }
    else if (now)
        g_MatrixMap->m_Cursor.SetPos(middle().x, middle().y);  // (a click's or the interface's own move put back)
    else
        g_centred = false;
    const bool in = driving() && g_MatrixMap->m_Camera.FirstPerson();
    g_way = std::clamp(g_way + (in ? 1.0f : -1.0f) * float(ms) / way_ms, 0.0f, 1.0f);
    ShaderPacks::set_mod_value("shooter", way_in());
    // The ring after where the hull looks, smoothly (just in: at once).
    D3DXVECTOR3 target;
    if (driving() && marker_target(target)) {
        if (!g_marker.shown) g_marker.at = target;
        g_marker.at += (target - g_marker.at) * (1.0f - std::exp(-float(std::max(ms, 0)) / marker_ms));
        g_marker.shown = true;
    }
    else
        g_marker.shown = false;
}

void release() {
    hold(false);
    g_centred = false;
    if (g_MatrixMap) g_MatrixMap->m_Cursor.SetPinned(false);
}

float way_in() { return g_way * g_way * (3.0f - 2.0f * g_way); }

D3DXVECTOR3 hull_aim(const CMatrixRobotAI &robot, const D3DXVECTOR3 &aim) {
    const D3DXVECTOR3 from(robot.m_PosX, robot.m_PosY, 0);
    D3DXVECTOR3 look(robot.m_HullForward.x, robot.m_HullForward.y, 0);
    if (D3DXVec3LengthSq(&look) < 1e-6f) return aim;
    D3DXVec3Normalize(&look, &look);
    const float reach = std::max(std::hypot(aim.x - from.x, aim.y - from.y), 20.0f);
    D3DXVECTOR3 at = from + look * reach;
    at.z = aim.z;
    return at;
}

void draw_marker() {
    if (!driving()) return;
    // The ring where the hull or head looks now.
    if (g_marker.shown) {
        begin_lines();
        DWORD cull = D3DCULL_CCW;
        g_D3DD->GetRenderState(D3DRS_CULLMODE, &cull);
        ASSERT_DX(g_D3DD->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));  // (a strip's triangles turn both ways)
        const D3DXVECTOR3 p = g_MatrixMap->m_Camera.ProjectNorm(g_marker.at);
        if (p.z > 0 && std::fabs(p.x) <= 1.5f && std::fabs(p.y) <= 1.5f) {  // not behind the eye or far off the screen
            // At the aim's pixel as the cursor's cross is drawn there: by its middle (between two pixels for a cross
            // two pixels thick), so that, the hull on the aim, the two are one around the other.
            const auto &cursor = g_MatrixMap->m_Cursor;
            const float x = (p.x * 0.5f + 0.5f) * float(g_ScreenX) + cursor.PinnedShiftX(),
                        y = (0.5f - p.y * 0.5f) * float(g_ScreenY) + cursor.PinnedShiftY();
            // A ring 3 pixels wide, edged dark on both sides (seen over the sky and over snow): filled bands, the same
            // on every side (lines were drawn a pixel heavier on the left).
            const float radius = marker_radius;
            constexpr int sides = 48;
            D3DXVECTOR4 band[2 * (sides + 1)];
            for (const auto &[inner, outer, color] :
                 {std::tuple{radius - 2.5f, radius - 1.5f, marker_shade},
                  std::tuple{radius - 1.5f, radius + 1.5f, marker_color},
                  std::tuple{radius + 1.5f, radius + 2.5f, marker_shade}}) {
                for (int i = 0; i <= sides; ++i) {
                    const float a = float(i) * 2.0f * float(M_PI_MUL(1)) / float(sides);
                    const float c = std::cos(a), s = std::sin(a);
                    band[2 * i] = D3DXVECTOR4(x + outer * c, y + outer * s, 0.0f, 1.0f);
                    band[2 * i + 1] = D3DXVECTOR4(x + inner * c, y + inner * s, 0.0f, 1.0f);
                }
                ASSERT_DX(g_D3DD->SetRenderState(D3DRS_TEXTUREFACTOR, color));
                ASSERT_DX(g_D3DD->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2 * sides, band, sizeof(D3DXVECTOR4)));
            }
        }
        ASSERT_DX(g_D3DD->SetRenderState(D3DRS_CULLMODE, cull));
        end_lines();
    }
}

}  // namespace Shooter
