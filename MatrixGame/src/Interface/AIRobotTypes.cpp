// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "CConstructor.h"

#include <stdexcept>

void SPrice::SetPrice(ERobotUnitType type, ERobotUnitKind kind) {
    ZeroMemory(m_Resources, sizeof(m_Resources));
    if (!kind)
        return;
    switch (type) {
        case MRT_HEAD: {
            int i = int(kind) - 1;
            m_Resources[TITAN] = g_Config.m_Price[HEAD1_TITAN + i * 4];
            m_Resources[ELECTRONICS] = g_Config.m_Price[HEAD1_ELECTRONICS + i * 4];
            m_Resources[ENERGY] = g_Config.m_Price[HEAD1_ENERGY + i * 4];
            m_Resources[PLASMA] = g_Config.m_Price[HEAD1_PLASM + i * 4];
        } break;
        case MRT_WEAPON: {
            int i = int(kind) - 1;
            m_Resources[TITAN] = g_Config.m_Price[WEAPON1_TITAN + i * 4];
            m_Resources[ELECTRONICS] = g_Config.m_Price[WEAPON1_ELECTRONICS + i * 4];
            m_Resources[ENERGY] = g_Config.m_Price[WEAPON1_ENERGY + i * 4];
            m_Resources[PLASMA] = g_Config.m_Price[WEAPON1_PLASM + i * 4];

        } break;
        case MRT_ARMOR: {
            int i = int(kind) - 1;
            m_Resources[TITAN] = g_Config.m_Price[ARMOR1_TITAN + i * 4];
            m_Resources[ELECTRONICS] = g_Config.m_Price[ARMOR1_ELECTRONICS + i * 4];
            m_Resources[ENERGY] = g_Config.m_Price[ARMOR1_ENERGY + i * 4];
            m_Resources[PLASMA] = g_Config.m_Price[ARMOR1_PLASM + i * 4];
        } break;
        case MRT_CHASSIS: {
            int i = int(kind) - 1;
            m_Resources[TITAN] = g_Config.m_Price[CHASSIS1_TITAN + i * 4];
            m_Resources[ELECTRONICS] = g_Config.m_Price[CHASSIS1_ELECTRONICS + i * 4];
            m_Resources[ENERGY] = g_Config.m_Price[CHASSIS1_ENERGY + i * 4];
            m_Resources[PLASMA] = g_Config.m_Price[CHASSIS1_PLASM + i * 4];

        } break;
    }
}

SSpecialBot *SSpecialBot::m_AIRobotTypeList = NULL;
int SSpecialBot::m_AIRobotTypeCnt = 0;

void SSpecialBot::LoadAIRobotType(CBlockPar &bp, std::span<const SRobotWeaponMatrix> weaponMatrix)
{
    ClearAIRobotType();

    int k, u;
    int cnt = bp.ParCount();
    m_AIRobotTypeList = (SSpecialBot *)HAllocClear(cnt * sizeof(SSpecialBot), g_MatrixHeap);

    int i;
    for (i = 0; i < cnt; i++) {
        auto str = bp.ParGet(i);
        if (str.GetCountPar(L",") < 2)
            continue;

        std::wstring str2;

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Pripor = bp.ParGetName(i).GetInt();
        if (m_AIRobotTypeList[m_AIRobotTypeCnt].m_Pripor < 1)
            ERROR_S2(L"LoadAIRobotType Pripor no=", utils::format(L"%d", i).c_str());

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nType = MRT_CHASSIS;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_UNKNOWN;

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nType = MRT_ARMOR;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_UNKNOWN;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxCommonWeaponCnt = 0;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxExtraWeaponCnt = 0;

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nType = MRT_HEAD;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind = RUK_UNKNOWN;

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_HaveBomb = false;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_HaveRepair = false;

        for (u = 0; u < MAX_WEAPON_CNT; u++) {
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nType = MRT_EMPTY;
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_UNKNOWN;
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Pos = u;
        }

        for (k = 0; k < MAX_RESOURCES; k++)
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Resources[k] = 0;

        str2 = utils::trim(str.GetStrPar(0, L","));
        if (str2 == L"Pneumatic" || str2 == L"P")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_CHASSIS_PNEUMATIC;
        else if (str2 == L"Whell" || str2 == L"W")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_CHASSIS_WHEEL;
        else if (str2 == L"Track" || str2 == L"T")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_CHASSIS_TRACK;
        else if (str2 == L"Hovercraft" || str2 == L"H")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_CHASSIS_HOVERCRAFT;
        else if (str2 == L"Antigravity" || str2 == L"A")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind = RUK_CHASSIS_ANTIGRAVITY;
        else
            ERROR_S2(L"LoadAIRobotType Chassis no=", utils::format(L"%d", i).c_str());

        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_Price.SetPrice(
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nType,
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_nKind);
        for (k = 0; k < MAX_RESOURCES; k++)
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Resources[k] +=
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Chassis.m_Price.m_Resources[k];

        str2 = utils::trim(str.GetStrPar(1, L","));

        if (str2 == L"1")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_MONOSTACK;
        else if (str2 == L"1S")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_BIREX;
        else if (str2 == L"2")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_DIPLOID;
        else if (str2 == L"2S")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_PARAGON;
        else if (str2 == L"3")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_TRIDENT;
        else if (str2 == L"4S")
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind = RUK_ARMOR_FULLSTACK;
        else
            ERROR_S2(L"LoadAIRobotType Armor no=", utils::format(L"%d", i).c_str());

        const auto armor = m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind - 1;
        if (armor >= weaponMatrix.size())
            throw std::out_of_range("AI robot definition missing armor capacity: " + std::to_string(armor + 1));
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxCommonWeaponCnt = weaponMatrix[armor].common;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxExtraWeaponCnt = weaponMatrix[armor].extra;
        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_Price.SetPrice(
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nType,
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_nKind);
        for (k = 0; k < MAX_RESOURCES; k++)
            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Resources[k] +=
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_Unit.m_Price.m_Resources[k];

        if (str.GetCountPar(L",") >= 3) {
            str2 = utils::trim(str.GetStrPar(2, L","));

            if (str2.length() > m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxCommonWeaponCnt +
                                        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxExtraWeaponCnt)
                ERROR_S2(L"LoadAIRobotType WeaponCnt no=", utils::format(L"%d", i).c_str());

            int cntnormal = 0;
            int cntextra = 0;

            for (u = 0; u < str2.length(); u++) {
                wchar ch = str2[u];
                if (ch == L'G') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_MACHINEGUN;
                }
                else if (ch == L'C') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_CANNON;
                }
                else if (ch == L'M') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_MISSILE;
                }
                else if (ch == L'F') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_FLAMETHROWER;
                }
                else if (ch == L'O') {
                    cntextra++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_MORTAR;
                }
                else if (ch == L'L') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_LASER;
                }
                else if (ch == L'B') {
                    cntextra++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_BOMB;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_HaveBomb = true;
                }
                else if (ch == L'P') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_PLASMA;
                }
                else if (ch == L'E') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_ELECTRIC;
                }
                else if (ch == L'R') {
                    cntnormal++;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind = RUK_WEAPON_REPAIR;
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_HaveRepair = true;
                }
                else
                    ERROR_S2(L"LoadAIRobotType WeaponType no=", utils::format(L"%d", i).c_str());

                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nType = MRT_WEAPON;

                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_Price.SetPrice(
                        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nType,
                        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_nKind);
                for (k = 0; k < MAX_RESOURCES; k++)
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Resources[k] +=
                            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Weapon[u].m_Unit.m_Price.m_Resources[k];
            }

            if (cntnormal > m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxCommonWeaponCnt)
                ERROR_S2(L"LoadAIRobotType WeaponCnt no=", utils::format(L"%d", i).c_str());
            if (cntextra > m_AIRobotTypeList[m_AIRobotTypeCnt].m_Armor.m_MaxExtraWeaponCnt)
                ERROR_S2(L"LoadAIRobotType WeaponExtraCnt no=", utils::format(L"%d", i).c_str());
        }

        if (str.GetCountPar(L",") >= 4) {
            str2 = utils::trim(str.GetStrPar(3, L","));

            if (str2 == L"Strength" || str2 == L"S")
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind = RUK_HEAD_BLOCKER;
            else if (str2 == L"Dynamo" || str2 == L"D")
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind = RUK_HEAD_DYNAMO;
            else if (str2 == L"Locator" || str2 == L"L")
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind = RUK_HEAD_LOCKATOR;
            else if (str2 == L"Firewall" || str2 == L"F")
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind = RUK_HEAD_FIREWALL;
            // else if(str2==L"Rapid" || str2==L"R") m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind=RUK_HEAD_RAPID;
            // else if(str2==L"Design" || str2==L"D")
            // m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind=RUK_HEAD_DESIGN; else if(str2==L"Speaker" ||
            // str2==L"P") m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind=RUK_HEAD_SPEAKER;
            else
                ERROR_S2(L"LoadAIRobotType Head no=", utils::format(L"%d", i).c_str());

            m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_Price.SetPrice(
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nType,
                    m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_nKind);
            for (k = 0; k < MAX_RESOURCES; k++)
                m_AIRobotTypeList[m_AIRobotTypeCnt].m_Resources[k] +=
                        m_AIRobotTypeList[m_AIRobotTypeCnt].m_Head.m_Price.m_Resources[k];
        }

        m_AIRobotTypeList[m_AIRobotTypeCnt].CalcStrength();

        m_AIRobotTypeCnt++;
    }

    m_AIRobotTypeList =
            (SSpecialBot *)HAllocEx(m_AIRobotTypeList, m_AIRobotTypeCnt * sizeof(SSpecialBot), g_MatrixHeap);

    // Сортируем по силе
    for (i = 0; i < m_AIRobotTypeCnt - 1; i++) {
        for (u = i + 1; u < m_AIRobotTypeCnt; u++) {
            if (m_AIRobotTypeList[u].m_Strength > m_AIRobotTypeList[i].m_Strength) {
                SSpecialBot temp = m_AIRobotTypeList[u];
                m_AIRobotTypeList[u] = m_AIRobotTypeList[i];
                m_AIRobotTypeList[i] = temp;
            }
        }
    }
}

void SSpecialBot::ClearAIRobotType() {
    if (m_AIRobotTypeList) {
        HFree(m_AIRobotTypeList, g_MatrixHeap);
        m_AIRobotTypeList = NULL;
    }
    m_AIRobotTypeCnt = 0;
}

void SSpecialBot::CalcStrength()  // Расчитываем силу робота
{
    m_Strength = 0.0f;

    for (int i = 0; i < MAX_WEAPON_CNT; i++) {
        if (m_Weapon[i].m_Unit.m_nType != MRT_WEAPON)
            continue;

        m_Strength += g_Config.m_WeaponStrengthAI[m_Weapon[i].m_Unit.m_nKind];
    }
}
