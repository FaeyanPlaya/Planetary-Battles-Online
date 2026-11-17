// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "MatrixMap.hpp"

#include "CFile.hpp"
#include "InterfaceHide.hpp"

#define CURSOR_Z 0.0f

void CMatrixCursor::SetVisible(bool flag) {
    // The interface hidden (F1): the cursor too. The side's logic asks for it at every step (against the AI every
    // 10 ms), and a system cursor shown till the frame hid it again kept appearing.
    if (flag && InterfaceHide::hidden())
        flag = false;
    // Partial battlefield cleanup may drop orders before Select loaded a cursor.
    if ((!m_CursorIcons && !m_CursorTexture) || !g_D3DD) return;
    if (!g_Config.m_SoftwareCursor && !m_Pinned) {
        if (FLAG(m_CursorFlags, CURSOR_VISIBLE) && !flag) {
            while (ShowCursor(false) >= 0)
                ;
            g_D3DD->ShowCursor(false);
            SetCursor(NULL);
        }
        else if (!FLAG(m_CursorFlags, CURSOR_VISIBLE) && flag) {
            SetCursor(m_CursorIcons[m_Frame]);
            int cntTry = 0;
            int cntCurs = ShowCursor(true);
            int cntCursPrev = cntCurs - 1;
            while (cntCurs < 0) {
                if (cntCurs == cntCursPrev)
                    ++cntTry;
                if (cntTry > 100) {
                    g_Config.m_SoftwareCursor = true;
                    m_CurCursor.clear();
                    Select(CURSOR_ARROW);
                    INITFLAG(m_CursorFlags, CURSOR_VISIBLE, flag);
                    return;
                }
                cntCursPrev = cntCurs;
                cntCurs = ShowCursor(true);
            }
            g_D3DD->ShowCursor(true);
            ShowCursor(true);
        }
    }
    INITFLAG(m_CursorFlags, CURSOR_VISIBLE, flag);
}

void CMatrixCursor::Select(const std::wstring& name) {
    DTRACE();

    if (m_CurCursor == name)
    {
        return;
    }
    DCP();
    m_CurCursor = name;

    Clear();
    DCP();

    m_Frame = 0;

    ParamParser n;
    DCP();
    int idx;
    for (idx = 0; idx < g_Config.m_CursorsCnt; ++idx) {
        if (g_Config.m_Cursors[idx].key == name) {
            n = g_Config.m_Cursors[idx].val;

            m_HotSpot.x = n.GetStrPar(1, L"?").GetStrPar(0, L",").GetInt();
            m_HotSpot.y = n.GetStrPar(1, L"?").GetStrPar(1, L",").GetInt();

            m_FramesCnt = n.GetStrPar(1, L"?").GetStrPar(2, L",").GetInt();
            m_CursorSize = n.GetStrPar(1, L"?").GetStrPar(3, L",").GetInt();
            m_CursorTimePeriod = n.GetStrPar(1, L"?").GetStrPar(4, L",").GetInt();

            if (m_FramesCnt < 0) {
                m_FramesCnt = -m_FramesCnt;
                SETFLAG(m_CursorFlags, CURSOR_REVERSEANIM);
            }
            else
                RESETFLAG(m_CursorFlags, CURSOR_REVERSEANIM);

            ASSERT(m_FramesCnt >= 1);

            m_FrameInc = 1;

            n = n.GetStrPar(0, L"?");

            break;
        }
    }
    DCP();

    // Pinned (the Shooter's aim in the screen's middle) a cross is put there by its middle: its hot spot was a few
    // pixels off it (the aim's ring came to the left of the cross seen).
    m_PinSpot = m_HotSpot;
    m_PinMiddleX = float(m_HotSpot.x);
    m_PinMiddleY = float(m_HotSpot.y);
    m_PinReach = 0;
    const bool cross = name.rfind(L"cross", 0) == 0;
    if (g_Config.m_SoftwareCursor) {
        LoadTexture(n.c_str());
        while (ShowCursor(false) >= 0)
            ;
        g_D3DD->ShowCursor(false);
        if (cross) {
            std::wstring tn;
            CBitmap bm;
            if (CFile::FileExist(tn, n.c_str(), CacheExtsTex) && bm.LoadFromPNG(tn.c_str()))
                m_PinSpot = PictureMiddle(bm);
        }
    }
    else {
        WinBitmap frame;
        frame.CreateRGBA(m_CursorSize, m_CursorSize);
        CBitmap bm;
        DCP();

        std::wstring tn;
        DCP();

        CFile::FileExist(tn, n.c_str(), CacheExtsTex);
        DCP();

        bm.LoadFromPNG(tn.c_str());
        if (cross)
            m_PinSpot = PictureMiddle(bm);
        DCP();

        int i = 0, x = 0, y = 0;

        m_CursorIcons = (HICON *)HAlloc(sizeof(HICON) * m_FramesCnt, g_MatrixHeap);

        ICONINFO ii;
        ii.fIcon = FALSE;
        ii.xHotspot = m_HotSpot.x;
        ii.yHotspot = m_HotSpot.y;
        do {
            if (x + m_CursorSize > bm.SizeX()) {
                x = 0;
                y += m_CursorSize;
            }
            if (y + m_CursorSize > bm.SizeY()) {
                break;
            }
            frame.Copy(CPoint(0, 0), frame.Size(), bm, CPoint(x, y));
            frame.Init();
            DCP();

            ii.hbmMask = frame.GetHandle();
            ii.hbmColor = frame.GetHandle();

            m_CursorIcons[i++] = CreateIconIndirect(&ii);
            DCP();

            x += m_CursorSize;
        }
        while (i < m_FramesCnt);

        if (m_Pinned) {
            LoadTexture(n.c_str());
            HideSystemCursor();
        }
        else {
            SetCursor(m_CursorIcons[0]);
            if (FLAG(m_CursorFlags, CURSOR_VISIBLE))
                while (ShowCursor(true) < 0)
                    ;
        }
        DCP();
    }
}

void CMatrixCursor::LoadTexture(const wchar_t *file) {
    m_CursorTexture = (CTextureManaged *)g_Cache->Get(CacheClass::TextureManaged, file);
    m_CursorTexture->MipmapOff();
    m_CursorTexture->Load();
    m_CursorInTexLine = m_CursorTexture->GetSizeX() / m_CursorSize;
    m_TexSizeXInversed = 1.0f / float(m_CursorTexture->GetSizeX());
    m_TexSizeYInversed = 1.0f / float(m_CursorTexture->GetSizeY());
    CalcUV();
}

// The middle of what the first frame shows (its opaque pixels' box): the pixel at it or left and above it (the exact
// middle kept: m_PinMiddleX, Y); the hot spot without an alpha to tell.
CPoint CMatrixCursor::PictureMiddle(const CBitmap &bm) {
    if (bm.BytePP() != 4 || !bm.Data())
        return m_HotSpot;
    const int sx = std::min(m_CursorSize, bm.SizeX()), sy = std::min(m_CursorSize, bm.SizeY());
    int x0 = sx, y0 = sy, x1 = -1, y1 = -1;
    for (int y = 0; y < sy; ++y) {
        const uint8_t *row = bm.Data() + y * bm.Pitch();
        for (int x = 0; x < sx; ++x) {
            if (row[x * 4 + 3] < 64)
                continue;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    if (x1 < 0)
        return m_HotSpot;
    m_PinMiddleX = float(x0 + x1) * 0.5f;
    m_PinMiddleY = float(y0 + y1) * 0.5f;
    m_PinReach = float(std::max(x1 - x0, y1 - y0) + 1) * 0.5f;
    return CPoint((x0 + x1) / 2, (y0 + y1) / 2);
}

void CMatrixCursor::HideSystemCursor() {
    while (ShowCursor(false) >= 0)
        ;
    g_D3DD->ShowCursor(false);
    SetCursor(NULL);
}

void CMatrixCursor::SetPinned(bool pinned) {
    if (pinned == m_Pinned)
        return;
    m_Pinned = pinned;
    // The software cursor is the game's drawing already; no cursor chosen yet: nothing to change.
    if (g_Config.m_SoftwareCursor || !g_D3DD || m_CurCursor.empty())
        return;
    // Chosen again: the picture loaded for the game to draw (pinned) or the Windows icons shown again (not).
    const std::wstring name = m_CurCursor;
    m_CurCursor.clear();
    Select(name);
    if (!pinned) {
        if (m_CursorTexture) {
            m_CursorTexture->Unload();
            m_CursorTexture = NULL;
        }
        if (FLAG(m_CursorFlags, CURSOR_VISIBLE)) {
            RESETFLAG(m_CursorFlags, CURSOR_VISIBLE);
            SetVisible(true);
        }
    }
}

void CMatrixCursor::CalcUV(void) {
    int y = TruncFloat(float(m_Frame * m_CursorSize) * m_TexSizeXInversed);
    int x = m_Frame - y * m_CursorInTexLine;

    m_u0 = float(x * m_CursorSize) * m_TexSizeXInversed + m_TexSizeXInversed * 0.5f;
    m_v0 = float(y * m_CursorSize) * m_TexSizeYInversed + m_TexSizeYInversed * 0.5f;

    m_u1 = float((x + 1) * m_CursorSize) * m_TexSizeXInversed - m_TexSizeXInversed * 0.5f;
    m_v1 = float((y + 1) * m_CursorSize) * m_TexSizeYInversed - m_TexSizeYInversed * 0.5f;
}

void CMatrixCursor::Takt(int ms) {
    DTRACE();

    m_Time += ms;

    if (m_FramesCnt < 2)
        return;
    int oldframe = m_Frame;
    while (m_Time > m_NextCursorTime) {
        m_NextCursorTime += m_CursorTimePeriod;

        if (!FLAG(m_CursorFlags, CURSOR_REVERSEANIM)) {
            ++m_Frame;
            if (m_Frame >= m_FramesCnt)
                m_Frame = 0;
        }
        else {
            m_Frame += m_FrameInc;
            if (m_FrameInc > 0) {
                if (m_Frame >= m_FramesCnt) {
                    m_Frame -= 2;
                    m_FrameInc = -m_FrameInc;
                }
            }
            else {
                if (m_Frame < 0) {
                    m_Frame += 2;
                    m_FrameInc = -m_FrameInc;
                }
            }
        }
    }
    if (oldframe != m_Frame && FLAG(m_CursorFlags, CURSOR_VISIBLE)) {
        if (!g_Config.m_SoftwareCursor && !m_Pinned) {
            // switch hardware icon
            SetCursor(m_CursorIcons[m_Frame]);
        }
        else {
            CalcUV();
        }
    }
}

void CMatrixCursor::Clear(void) {
    SetCursor(m_OldCursor);

    if (m_CursorTexture) {
        m_CursorTexture->Unload();
        m_CursorTexture = NULL;
    }

    if (m_CursorIcons) {
        for (int i = 0; i < m_FramesCnt; ++i) {
            DestroyIcon(m_CursorIcons[i]);
        }
        HFree(m_CursorIcons, g_MatrixHeap);
        m_CursorIcons = NULL;
    }
}

void CMatrixCursor::Draw(void) const
{
    DTRACE();

    if ((g_Config.m_SoftwareCursor || m_Pinned) && m_CursorTexture && FLAG(m_CursorFlags, CURSOR_VISIBLE)) {
        SVert_V4_UV v[4];

        v[0].tu = m_u0;
        v[0].tv = m_v1;
        v[1].tu = m_u0;
        v[1].tv = m_v0;
        v[2].tu = m_u1;
        v[2].tv = m_v1;
        v[3].tu = m_u1;
        v[3].tv = m_v0;

        SetAlphaOpSelect(0, D3DTA_TEXTURE);
        SetColorOpSelect(0, D3DTA_TEXTURE);
        SetColorOpDisable(1);

        g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_D3DD->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

        ASSERT_DX(g_D3DD->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        ASSERT_DX(g_D3DD->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));

        // ASSERT_DX(g_D3DD->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF));

        const CPoint &spot = m_Pinned ? m_PinSpot : m_HotSpot;
        int x0 = m_Pos.x - spot.x;
        int y0 = m_Pos.y - spot.y;
        int x1 = x0 + m_CursorSize;
        int y1 = y0 + m_CursorSize;

        v[0].p = D3DXVECTOR4(float(x0) - 0.5f, float(y1) - 0.5f, CURSOR_Z, 1.0f);
        v[1].p = D3DXVECTOR4(float(x0) - 0.5f, float(y0) - 0.5f, CURSOR_Z, 1.0f);
        v[2].p = D3DXVECTOR4(float(x1) - 0.5f, float(y1) - 0.5f, CURSOR_Z, 1.0f);
        v[3].p = D3DXVECTOR4(float(x1) - 0.5f, float(y0) - 0.5f, CURSOR_Z, 1.0f);

        CInstDraw::BeginDraw(IDFVF_V4_UV);
        CInstDraw::AddVerts(v, m_CursorTexture);
        CInstDraw::ActualDraw();

        g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_D3DD->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

        g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    }
}
