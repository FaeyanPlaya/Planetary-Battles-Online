// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "MatrixMap.hpp"

#define CURSOR_VISIBLE     1
#define CURSOR_REVERSEANIM 2

class CMatrixCursor : public CMain {
    DWORD m_CursorFlags;
    CPoint m_Pos;
    CPoint m_HotSpot;
    std::wstring m_CurCursor;
    CTextureManaged *m_CursorTexture;
    int m_CursorSize;
    HICON *m_CursorIcons;
    HCURSOR m_OldCursor;
    int m_FramesCnt;
    int m_FrameInc;
    int m_Frame;
    int m_NextCursorTime;
    int m_CursorTimePeriod;
    int m_CursorInTexLine;
    float m_TexSizeXInversed, m_TexSizeYInversed;
    float m_u0, m_v0, m_u1, m_v1;
    bool m_Pinned;  // drawn by the game (as the software cursor is), the Windows cursor hidden
    CPoint m_PinSpot;  // pinned: the picture's point put on the position (a cross's middle, not its hot spot)
    float m_PinMiddleX, m_PinMiddleY;  // pinned: the picture's middle in it (between two pixels for an even one)
    float m_PinReach;  // how far from that middle the picture goes (its opaque box's half, to the pixels' edges)
    void CalcUV(void);
    void LoadTexture(const wchar_t *file);
    static void HideSystemCursor();
    CPoint PictureMiddle(const CBitmap &bm);

    int m_Time;

public:
    CMatrixCursor(void) {
        memset(this, 0, sizeof(CMatrixCursor));
        m_OldCursor = SetCursor(NULL);
    }
    ~CMatrixCursor(void) { Clear(); }

    void Draw(void) const;

    void Select(const std::wstring& name);
    void Takt(int ms);
    void Clear(void);
    void SetVisible(bool flag);
    bool IsVisible() const { return (m_CursorFlags & CURSOR_VISIBLE) != 0; }
    // Drawn by the game where its position is, not by Windows where the mouse is (the Shooter holds it in the
    // screen's middle while the mouse turns the view: Windows' cursor shook between the moves put back there).
    void SetPinned(bool pinned);
    // Pinned: how far from its position the middle of the picture drawn is (half a pixel for a cross two pixels thick),
    // in pixels; nothing otherwise.
    float PinnedShiftX() const { return m_Pinned ? m_PinMiddleX - float(m_PinSpot.x) : 0.0f; }
    float PinnedShiftY() const { return m_Pinned ? m_PinMiddleY - float(m_PinSpot.y) : 0.0f; }
    // Pinned: how far from its middle the picture goes, in pixels (0 not known or not pinned).
    float PinnedReach() const { return m_Pinned ? m_PinReach : 0.0f; }
    // The cursor chosen now (CURSOR_ARROW, CURSOR_CROSS_RED...).
    const std::wstring &Current() const { return m_CurCursor; }

    const CPoint &GetPos(void) const { return m_Pos; }
    int GetPosX(void) const { return m_Pos.x; }
    int GetPosY(void) const { return m_Pos.y; }
    void SetPos(const CPoint &pos) { SetPos(pos.x, pos.y); }
    void SetPos(int xx, int yy) {
        m_Pos.x = xx;
        m_Pos.y = yy;
    }
};
