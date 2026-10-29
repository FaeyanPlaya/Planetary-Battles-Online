// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#pragma once

#include "MatrixProgressBar.hpp"
#include "Effects/MatrixEffect.hpp"

class CMatrixBuilding;
class CMatrixRovotAI;
class CMatrixSideUnit;
class CMatrixEffectSelection;

#define CANNON_FIRE_THINK_PERIOD 100
#define CANNON_NULL_TARGET_TIME  1000
#define CANNON_TIME_FROM_FIRE    1000

#define CANNNON_MIN_DANGLE GRAD2RAD(2)

#define CANNON_COLLIDE_R 20

// A dismantled turret (TurretControl) sinks this long, at this speed (units per ms), before it goes.
constexpr int dismantle_time = 1500;
constexpr float dismantle_sink_speed = 0.03f;
// The selection ring around a selected turret (TurretControl).
constexpr float turret_selection_size = 30.0f;
enum ECannonState {
    CANNON_IDLE,
    CANNON_UNDER_CONSTRUCTION,
    CANNON_DIP,

    ECannonState_FORCE_DWORD = 0x7FFFFFFF
};

enum ECannonUnitType {
    CUT_EMPTY,
    CUT_BASIS,
    CUT_TURRET,
    CUT_SHAFT,

    ECannonUnitType_FORCE_DWORD = 0x7FFFFFFF
};

#define MR_MAXCANNONUNIT 3

struct SMatrixCannonUnit {
#ifdef _DEBUG
    SMatrixCannonUnit(void) : m_Smoke(DEBUG_CALL_INFO) {}
#else
    SMatrixCannonUnit() = default;
#endif

    ECannonUnitType m_Type;

    CVectorObjectAnim *m_Graph;
    D3DXMATRIX m_Matrix;
    SEffectHandler m_Smoke;

    union _u1 {
        _u1() {};

        struct _s1 {
            _s1() {};

            CVOShadowStencil *m_ShadowStencil;
            float m_Angle;
            int m_LinkMatrix;
            D3DXMATRIX m_IMatrix{};
            DWORD m_Invert;
        } s1;
        struct _s2 {
            _s2() {};

            D3DXVECTOR3 m_Pos{};
            D3DXVECTOR3 m_Velocity{};
            float m_TTL;
            float m_dp, m_dr, m_dy;
        } s2;
    } u1;
};

class CMatrixCannon : public CMatrixMapStatic {
    static void FireHandler(CMatrixMapStatic *hit, const D3DXVECTOR3 &pos, uintptr_t user, DWORD flags);

protected:
    // hitpoint
    CMatrixProgressBar m_PB;
    int m_ShowHitpointTime;
    float m_HitPoint;
    float m_HitPointMax;  // Максимальное кол-во здоровья
    union {
        float m_MaxHitPointInversed;  // for normalized calcs
        float m_AngleMustBe;
    };

    int m_UnderAttackTime;

public:
    EShadowType m_ShadowType;  // 0-off 1-proj 2-proj with anim 3-stencil
    int m_ShadowSize;          // texture size for proj

    CMatrixBuilding *m_ParentBuilding;
    D3DXVECTOR2 m_Pos;

    int m_Place;  // Место, в котором установлена пушка. (Всегда должно быть инициализировано)
private:
    int m_Side;  // 1-8
public:
    int m_Num;

    float m_AddH;
    float m_Angle;
    float m_AngleX;

    int m_FireNextThinkTime;
    int m_NullTargetTime;
    int m_TimeFromFire;

    int m_UnitCnt;
    SMatrixCannonUnit m_Unit[MR_MAXCANNONUNIT];

    CMatrixShadowProj *m_ShadowProj;

    ECannonState m_CurrState;

    int m_NextTimeAblaze;
    int m_NextTimeShorted;

    D3DXVECTOR3 m_TargetDisp;

    D3DXVECTOR3 m_FireCenter;
    D3DXVECTOR3 m_FireFrom[2];
    D3DXVECTOR3 m_FireDir[2];

    CMatrixEffectWeapon *m_Weapons[2];

    int m_WeaponCnt;
    int m_FireMatrix[2];

    SObjectCore *m_TargetCore;
    // TurretControl: the unit a person ordered the turret to fire at (null: the turret chooses).
    SObjectCore *m_OrderCore = nullptr;
    // Dismantled (TurretControl): no wreck, the turret sinks into its place and goes.
    bool m_Dismantled = false;
    // Selected by this client's person (TurretControl): the ring around it.
    CMatrixEffectSelection *m_Selection = nullptr;

    int m_LastDelayDamageSide;
    int m_MiniMapFlashTime;

    void ReleaseMe();

    void BeginFireAnimation(void);
    void EndFireAnimation(void);

    // A person in the turret: it turns to their aim and fires while they hold the button.
    void DriveTakt(int takt, const CMatrixSideUnit &driver);
    // Turns the head and the barrel toward `pos` by one step, as the turret's own aiming does; true when aimed.
    bool TurnTo(const D3DXVECTOR3 &pos, int takt);
    // The weapons' step: fire effects, the hits' bookkeeping and the fire animation.
    void WeaponsTakt(int takt);

public:
    CMatrixCannon(void);
    ~CMatrixCannon();

    void DIPTakt(float ms);

    void ShowHitpoint(void) { m_ShowHitpointTime = HITPOINT_SHOW_TIME; }
    float GetHitPoint(void) const { return m_HitPoint; }
    float GetMaxHitPoint() { return m_HitPointMax; }
    void InitMaxHitpoint(float hp) {
        m_HitPoint = hp;
        m_HitPointMax = hp;
        m_MaxHitPointInversed = 1.0f / hp;
    }
    void SetHitPoint(float hp) { m_HitPoint = hp; }
    float GetMaxHitPointInversed() { return m_MaxHitPointInversed; }
    float GetSeekRadius(void);
    float GetFireRadius(void) {
        if (m_Weapons[0] == NULL)
            return 0;
        return m_Weapons[0]->GetWeaponDist();
    }

    bool IsRefProtect(void) const { return FLAG(m_ObjectState, OBJECT_CANNON_REF_PROTECTION); }
    void SetRefProtectHit(void) { SETFLAG(m_ObjectState, OBJECT_CANNON_REF_PROTECTION_HIT); }

    void SetPBOutOfScreen() { m_PB.Modify(100000.0f, 0); }

    void SetMustBeAngle(float a) { m_AngleMustBe = a; }
    float GetMustBeAngle(void) { return m_AngleMustBe; }

    void SetSide(int id) { m_Side = id; }

    void UnitInit(int num) {
        m_Num = num;
        RChange(MR_ShadowStencil | MR_ShadowProjGeom | MR_ShadowProjTex | MR_Graph);
    }

    void UnitClear(void);

    void BoundGet(D3DXVECTOR3 &bmin, D3DXVECTOR3 &bmax);

    virtual bool Damage(EWeapon weap, const D3DXVECTOR3 &pos, const D3DXVECTOR3 &dir, int attacker_side,
                        CMatrixMapStatic *attaker);
    virtual void RNeed(dword need);

    virtual void Takt(int cms);
    virtual void AnimationTakt(int cms);
    virtual void LogicTakt(int cms);
    void PauseTakt(int cms);

    virtual bool Pick(const D3DXVECTOR3 &orig, const D3DXVECTOR3 &dir, float *outt) const;

    virtual void BeforeDraw(void);
    virtual void Draw(void);
    virtual void DrawShadowStencil(void);
    virtual void DrawShadowProj(void);

    virtual void FreeDynamicResources(void);

    void OnLoad(void);

    virtual bool CalcBounds(D3DXVECTOR3 &omin, D3DXVECTOR3 &omax);
    virtual int GetSide(void) const { return m_Side; };
    virtual bool NeedRepair(void) const {
        return m_CurrState != CANNON_UNDER_CONSTRUCTION && (m_HitPoint < m_HitPointMax);
    }

    virtual bool InRect(const CRect &rect) const;

    void OnOutScreen(void){};

    float GetStrength(void);

    // TurretControl. A built turret, not dying: one a person may order, enter or dismantle.
    bool IsBuilt() const { return m_CurrState == CANNON_IDLE && !IsDIP(); }
    // The side whose person is in this turret, or null.
    CMatrixSideUnit *Driver() const;
    bool IsDriven() const { return Driver() != nullptr; }
    // Where the head looks (radians, as the camera's z angle counts it from the y axis).
    float HeadAngle() const { return m_Unit[1].u1.s1.m_Angle + m_Angle; }
    // Fire at `target` while it is in reach (null: choose targets again).
    void OrderAttack(CMatrixMapStatic *target);
    CMatrixMapStatic *OrderTarget() const { return m_OrderCore ? m_OrderCore->m_Object : nullptr; }
    // The manual control bonus: the weapons reach farther while a person is in the turret.
    void SetDriveBonus(bool on);
    // Dismantles the turret: its place frees at once, the turret sinks into the ground and is deleted.
    void Dismantle();
    // This client's selection: the ring, and the health bar in the panel.
    void Select();
    void UnSelect();
    void CreateProgressBarClone(float x, float y, float width, EPBCoord clone_type) {
        m_PB.CreateClone(clone_type, x, y, width);
    }
    void DeleteProgressBarClone(EPBCoord clone_type) { m_PB.KillClone(clone_type); }
    // The turret dies or goes: this client's person no longer has it selected.
    void LeaveSelection();
};
