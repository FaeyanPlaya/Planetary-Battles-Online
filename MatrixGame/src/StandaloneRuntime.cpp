// MatrixGame - licensed under GPLv2 or any later version.
#include "StandaloneRuntime.hpp"

#include "MatrixConfig.hpp"
#include "MatrixGameDll.hpp"
#include "SoundBridge.hpp"
#include "WindowDisplay.hpp"
#include "BattleRules.hpp"
#include "Commands.hpp"

namespace StandaloneSettings {
namespace {
Values saved;
Values started;
float camera_speed = 0.0f;  // Scroll speed configured by the game data.

void apply_immediate(const Values &values) {
    g_Config.m_ShowStencilShadows = values.stencil_shadows;
    g_Config.m_ShowProjShadows = values.projected_shadows;
    g_Config.m_CamMoveSpeed = camera_speed * static_cast<float>(values.camera_speed) / 100.0f;
    SoundBridge::master_volume(static_cast<float>(values.sound_volume) / 100.0f);
    SoundBridge::music_volume(static_cast<float>(values.music_volume) / 100.0f);
    if (battle_rules_locked())
        return;
    BattleRules::apply_modes(battle_rules(values));
}
}  // namespace

BattleRules::Rules battle_rules(const Values &values) {
    return {values.random, values.delayed_damage == DamageAttribution::Original,
            values.group_counts == GroupCountMode::Original,
            values.place_containment == PlaceContainmentMode::Original,
            values.zero_x_trace == ZeroXTraceMode::Original};
}

void apply_startup(const Values &values, SRobotsSettings &settings) {
    settings.m_ShowStencilShadows = values.stencil_shadows;
    settings.m_ShowProjShadows = values.projected_shadows;
    settings.m_SkyBox = static_cast<byte>(values.skybox);
    settings.m_RobotShadow = values.robot_shadows ? 1 : 0;
}

void initialize_runtime(const Values &values) {
    camera_speed = g_Config.m_CamMoveSpeed;  // ReadParams restores the data value for each session.
    saved = started = values;
    apply_immediate(values);
    if (const auto &rules = BattleRules::prescribed())
        BattleRules::apply_modes(*rules);  // the battle's rules, not this person's settings
}

void apply_runtime(const Values &values) {
    apply_immediate(values);
    if (Display::enabled()) {
        Display::request(values.display.mode, values.display.fullscreen);
        Display::request_size(values.display.width, values.display.height);
    }
    saved = values;
}

const Values &current() { return saved; }

bool restart_pending(const Values &values) {
    return values.random != started.random || values.skybox != started.skybox ||
           values.robot_shadows != started.robot_shadows ||
           (battle_rules_locked() && values.delayed_damage != started.delayed_damage);
}

bool battle_rules_locked() { return Commands::deferred() || BattleRules::prescribed().has_value(); }

}  // namespace StandaloneSettings
