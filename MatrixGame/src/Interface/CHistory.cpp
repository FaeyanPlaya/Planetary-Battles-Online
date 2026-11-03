// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "CHistory.h"
#include "../MatrixGame.h"
#include "../MatrixMap.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stupid_logger.hpp>
#include <utils.hpp>

CHistory *g_ConfigHistory;
int CHistory::m_Kept = 20;

CHistory::CHistory() {
    m_FirstConfig = NULL;
    m_LastConfig = NULL;
    m_CurrentConfig = NULL;
}

CHistory::~CHistory() {
    SRobotConfig *cfgs = m_FirstConfig;

    while (cfgs) {
        SRobotConfig *next = cfgs->m_NextConfig;
        HDelete(SRobotConfig, cfgs, g_MatrixHeap);
        cfgs = next;
    }
    m_FirstConfig = NULL;
    m_LastConfig = NULL;
}

void CHistory::AddConfig(SRobotConfig *config) {
    DTRACE();
    DelConfig(config);
    SRobotConfig *new_cfg = HNew(g_MatrixHeap) SRobotConfig;
    memcpy(new_cfg, config, sizeof(SRobotConfig));
    m_CurrentConfig = new_cfg;
    m_Shown = true;
    LIST_ADD(new_cfg, m_FirstConfig, m_LastConfig, m_PrevConfig, m_NextConfig);
    Save();
}

// A line: chassis, hull, head, the four common weapons and the extra one (kinds, 0: none).
void CHistory::Load(const std::wstring &file) {
    DTRACE();
    m_File = file;
    std::ifstream input{std::filesystem::path(file)};
    std::string line;
    int skipped = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '\r')
            continue;
        std::istringstream fields(line);
        int chassis = 0, hull = 0, head = 0, weapon[MAX_WEAPON_CNT] = {};
        fields >> chassis >> hull >> head;
        for (int &w : weapon) fields >> w;
        bool fits = !fields.fail() && chassis >= 1 && chassis <= ROBOT_CHASSIS_CNT && hull >= 1 &&
                    hull <= ROBOT_ARMOR_CNT && head >= 0 && head <= ROBOT_HEAD_CNT;
        for (int i = 0; i < MAX_WEAPON_CNT; ++i) {
            const int w = weapon[i];
            const bool extra = w == RUK_WEAPON_MORTAR || w == RUK_WEAPON_BOMB;
            fits = fits && w >= 0 && w <= ROBOT_WEAPON_CNT && (w == 0 || extra == (i == MAX_WEAPON_CNT - 1));
        }
        if (!fits) {
            ++skipped;
            continue;
        }
        SRobotConfig config;
        config.m_Chassis.m_nKind = ERobotUnitKind(chassis);
        config.m_Hull.m_Unit.m_nKind = ERobotUnitKind(hull);
        config.m_Head.m_nKind = ERobotUnitKind(head);
        for (int i = 0; i < MAX_WEAPON_CNT; ++i) config.m_Weapon[i].m_nKind = ERobotUnitKind(weapon[i]);
        DelConfig(&config);
        SRobotConfig *loaded = HNew(g_MatrixHeap) SRobotConfig;
        memcpy(loaded, &config, sizeof(SRobotConfig));
        LIST_ADD(loaded, m_FirstConfig, m_LastConfig, m_PrevConfig, m_NextConfig);
    }
    if (skipped)
        lgr.warning("{}: {} designs left out (not a design)")(utils::from_wstring(file), skipped);
    // Only the last m_Kept (fewer may be kept now than were).
    int count = 0;
    for (SRobotConfig *c = m_LastConfig; c; c = c->m_PrevConfig) ++count;
    for (; count > std::max(1, m_Kept); --count) {
        SRobotConfig *oldest = m_FirstConfig;
        LIST_DEL(oldest, m_FirstConfig, m_LastConfig, m_PrevConfig, m_NextConfig);
        HDelete(SRobotConfig, oldest, g_MatrixHeap);
    }
    m_CurrentConfig = m_LastConfig;
    m_Shown = !m_CurrentConfig;
}

void CHistory::Save() const {
    if (m_File.empty())
        return;
    int count = 0;
    const SRobotConfig *from = m_LastConfig;
    for (; from && from->m_PrevConfig && count < m_Kept - 1; ++count) from = from->m_PrevConfig;
    std::ostringstream text;
    text << "; Planetary Battles Online: the robots built here, the last at the end (kept: " << m_Kept << ")\n"
         << "; chassis hull head weapon1 weapon2 weapon3 weapon4 extra\n";
    for (const SRobotConfig *c = from; c; c = c->m_NextConfig) {
        text << c->m_Chassis.m_nKind << ' ' << c->m_Hull.m_Unit.m_nKind << ' ' << c->m_Head.m_nKind;
        for (int i = 0; i < MAX_WEAPON_CNT; ++i) text << ' ' << c->m_Weapon[i].m_nKind;
        text << '\n';
    }
    const std::filesystem::path target(m_File);
    auto temporary = target;
    temporary += L".tmp";
    std::error_code error;
    if (target.has_parent_path())
        std::filesystem::create_directories(target.parent_path(), error);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        const std::string bytes = text.str();
        stream.write(bytes.data(), std::streamsize(bytes.size()));
        if (!stream.good()) {
            lgr.warning("Cannot write {}: the designs are not kept")(utils::from_wstring(m_File));
            return;
        }
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        lgr.warning("Cannot replace {}: the designs are not kept")(utils::from_wstring(m_File));
        std::filesystem::remove(temporary, error);
    }
}

void CHistory::DelConfig(SRobotConfig *config) {
    DTRACE();
    SRobotConfig *confs = m_FirstConfig;
    while (confs) {
        bool gotcha = false;
        if (confs->m_Head.m_nKind == config->m_Head.m_nKind && confs->m_Chassis.m_nKind == config->m_Chassis.m_nKind &&
            confs->m_Hull.m_Unit.m_nKind == config->m_Hull.m_Unit.m_nKind) {
            gotcha = true;
        }

        if (gotcha) {
            for (int i = 0; i < MAX_WEAPON_CNT; i++) {
                if (confs->m_Weapon[i].m_nKind != config->m_Weapon[i].m_nKind) {
                    gotcha = false;
                    break;
                }
            }
        }

        if (gotcha) {
            SRobotConfig *next = confs->m_NextConfig;
            LIST_DEL(confs, m_FirstConfig, m_LastConfig, m_PrevConfig, m_NextConfig);
            HDelete(SRobotConfig, confs, g_MatrixHeap);
            confs = next;
            continue;
        }
        confs = confs->m_NextConfig;
    }
}

void CHistory::LoadCurrentConfigToConstructor() {
    DTRACE();
    if (m_CurrentConfig) {
        CMatrixSideUnit *ps = g_MatrixMap->GetPlayerSide();
        ps->m_Constructor->SuperDjeans(MRT_HEAD, m_CurrentConfig->m_Head.m_nKind, 0, true);
        ps->m_Constructor->SuperDjeans(MRT_ARMOR, m_CurrentConfig->m_Hull.m_Unit.m_nKind, 0, true);
        // (A kept design's weapons only where its hull has the slot: the file may have been written by hand.)
        const SRobotWeaponMatrix &slots = g_MatrixMap->m_RobotWeaponMatrix[m_CurrentConfig->m_Hull.m_Unit.m_nKind - 1];
        for (int i = 0; i < MAX_WEAPON_CNT; i++) {
            const bool slot = i == MAX_WEAPON_CNT - 1 ? slots.extra > 0 : i < slots.common;
            if (m_CurrentConfig->m_Weapon[i].m_nKind && slot)
                ps->m_Constructor->SuperDjeans(MRT_WEAPON, m_CurrentConfig->m_Weapon[i].m_nKind, i, true);
        }
        ps->m_Constructor->SuperDjeans(MRT_CHASSIS, m_CurrentConfig->m_Chassis.m_nKind, 0, true);
    }
}

const SRobotConfig *CHistory::StepBack() {
    if (m_CurrentConfig && !m_Shown) {  // a loaded design: the last built before, then back from it
        m_Shown = true;
        return m_CurrentConfig;
    }
    if (m_CurrentConfig && m_CurrentConfig->m_PrevConfig) {
        m_CurrentConfig = m_CurrentConfig->m_PrevConfig;
        return m_CurrentConfig;
    }
    return NULL;
}

void __stdcall CHistory::PrevConfig(void*) {
    DTRACE();
    if (StepBack())
        LoadCurrentConfigToConstructor();
}

void __stdcall CHistory::NextConfig(void*) {
    DTRACE();
    if (m_CurrentConfig && m_CurrentConfig->m_NextConfig) {
        m_CurrentConfig = m_CurrentConfig->m_NextConfig;
        LoadCurrentConfigToConstructor();
    }
}
