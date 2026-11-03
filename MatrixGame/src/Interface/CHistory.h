// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "CConstructor.h"
#include <string>

// The designs this person built (12, saved designs): the last m_Kept are kept in a file between battles and loaded at
// the start, so the history's arrows go back to them too.

class CHistory : public CMain {
    SRobotConfig *m_CurrentConfig;
    std::wstring m_File;  // where the designs are kept; empty: not kept (the original)
    bool m_Shown = true;  // false: the current one is a loaded design, not in the constructor yet

    void LoadCurrentConfigToConstructor();
    void DelConfig(SRobotConfig *config);
    void FindConfig(SRobotConfig *config);
    void Save() const;

public:
    static int m_Kept;  // the settings' SavedDesigns (1-50)

    SRobotConfig *m_FirstConfig;
    SRobotConfig *m_LastConfig;

    void AddConfig(SRobotConfig *config);
    // Takes the designs kept in `file` (from now on kept there): the last of them is the current one, put into the
    // constructor by the first step back. Lines it cannot take are left out.
    void Load(const std::wstring &file);
    const SRobotConfig *CurrentConfig() const { return m_CurrentConfig; }
    // The design a step back puts into the constructor (PrevConfig puts it in); NULL: none.
    const SRobotConfig *StepBack();
    void __stdcall PrevConfig(void *object);
    void __stdcall NextConfig(void *object);

    bool IsNext() {
        if (m_CurrentConfig && m_CurrentConfig->m_NextConfig)
            return true;
        else
            return false;
    }
    bool IsPrev() {
        if (m_CurrentConfig && (!m_Shown || m_CurrentConfig->m_PrevConfig))
            return true;
        else
            return false;
    }

    CHistory();
    ~CHistory();
};