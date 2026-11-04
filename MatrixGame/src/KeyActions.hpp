// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

// What a key does (CMatrixConfig::m_KeyActions holds each one's key). The game's config (AssignKey) numbers
// them: new ones go at the end.
enum EKeyAction : int {
    KA_SCROLL_LEFT,
    KA_SCROLL_RIGHT,
    KA_SCROLL_UP,
    KA_SCROLL_DOWN,

    KA_SCROLL_LEFT_ALT,
    KA_SCROLL_RIGHT_ALT,
    KA_SCROLL_UP_ALT,
    KA_SCROLL_DOWN_ALT,

    KA_ROTATE_LEFT,
    KA_ROTATE_RIGHT,
    KA_ROTATE_UP,
    KA_ROTATE_DOWN,

    KA_UNIT_FORWARD,
    KA_UNIT_BACKWARD,
    KA_UNIT_LEFT,
    KA_UNIT_RIGHT,

    KA_UNIT_FORWARD_ALT,
    KA_UNIT_BACKWARD_ALT,
    KA_UNIT_LEFT_ALT,
    KA_UNIT_RIGHT_ALT,

    KA_FIRE,
    KA_AUTO,

    KA_SHIFT,
    KA_CTRL,

    KA_ROTATE_LEFT_ALT,   // zak
    KA_ROTATE_RIGHT_ALT,  // zak

    KA_MINIMAP_ZOOMIN,   // sub
    KA_MINIMAP_ZOOMOUT,  // sub

    KA_CAM_SETDEFAULT,  // zak

    KA_AUTOORDER_CAPTURE,  // sub
    KA_AUTOORDER_ATTACK,   // sub
    KA_AUTOORDER_DEFEND,   // sub

    KA_ORDER_MOVE,           // sub
    KA_ORDER_STOP,           // sub
    KA_ORDER_CAPTURE,        // sub
    KA_ORDER_PATROL,         // sub
    KA_ORDER_EXPLODE,        // sub
    KA_ORDER_REPAIR,         // sub
    KA_ORDER_ATTACK,         // sub
    KA_ORDER_ROBOT_SWITCH1,  // sub
    KA_ORDER_ROBOT_SWITCH2,  // sub

    KA_ORDER_CANCEL,  // sub

    KA_UNIT_BOOM,       // sub
    KA_UNIT_ENTER,      // sub (! if not dialog mode)
    KA_UNIT_ENTER_ALT,  // sub (! if not dialog mode)

    KA_BUILD_ROBOT,   // sub
    KA_BUILD_TURRET,  // sub
    KA_BUILD_HELP,    // sub

    KA_TURRET_CANNON,
    KA_TURRET_GUN,
    KA_TURRET_LASER,
    KA_TURRET_ROCKET,

    // ManualExtras (new ones at the end: the config numbers keys)
    KA_UNIT_STRAFE_LEFT,
    KA_UNIT_STRAFE_RIGHT,
    KA_UNIT_BOOM_EXTRAS,  // the bomb with the mod on (E strafes)
    KA_UNIT_REAR_VIEW,

    // Mods' keys that were the code's own (key rebinding: every key a person may change is an action here)
    KA_UNIT_SLOT1,  // ManualExtras: a driven robot's weapon slots on and off, 1-5
    KA_UNIT_SLOT2,
    KA_UNIT_SLOT3,
    KA_UNIT_SLOT4,
    KA_UNIT_SLOT5,

    //..........................
    KA_LAST
};
