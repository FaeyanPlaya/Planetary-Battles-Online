// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#pragma once

#include <string>
#include <cstdint>

#include "CMain.hpp"

enum ButtonStatus {
    B_DOWN,
    B_UP,
    B_DOUBLE,

    B_WHEEL
};

enum ESysEvent {
    SYSEV_DEACTIVATING,
    SYSEV_ACTIVATED,
    SYSEV_INPUT_RESET,  // Release held keys without changing activity or graphics.
    SYSEV_POINTER_LEFT,
};

/**
 * @brief Probably represents an abstract logical Window.
 *
 * Is double-linked chained (i.e. it's an element of all other Forms Linked List.)
 *
 * Yet it seems like its ability to have multiple forms is never used,
 *      or it could be used inside of Map Editor, source code of which is not available.
 */
class CForm : public Base::CMain {
private:
    CForm *m_FormPrev;
    CForm *m_FormNext;

protected:
    std::wstring m_Name;  // Нужно всегда задавать имя для дочерних классов

public:
    static void StaticInit(void);

    CForm(void);
    ~CForm();

    virtual void Enter(void) = 0;
    virtual void Leave(void) = 0;

    /**
     * @brief Execute draw logic for this window?
     */
    virtual void Draw(void) = 0;

    /**
     * @brief Execute logic frame for this window?
     */
    virtual void Takt(int delta_ms) = 0;

    virtual void MouseMove(int x, int y) = 0;
    virtual void MouseKey(ButtonStatus status, int key, int x, int y) = 0;

    virtual void Keyboard(bool, uint8_t) = 0;

    virtual void SystemEvent(ESysEvent se) = 0;
};

extern CForm *g_FormFirst;
extern CForm *g_FormLast;
extern CForm *g_FormCur;

void FormChange(CForm *form);
