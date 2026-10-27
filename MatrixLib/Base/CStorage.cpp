// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include <new>

#include "CStorage.hpp"
#include "CRC32.hpp"

#undef ZEXPORT
#define ZEXPORT __cdecl

#include "zlib.h"

namespace Base {

static int ZL03_UnCompress(CBuf &out, BYTE *in, int inlen) {
    out.Clear();

    if (inlen < 8)
        return 0;
    if (*(in + 0) != 'Z' || *(in + 1) != 'L' || *(in + 2) != '0' || *(in + 3) != '3')
        return 0;

    int cnt = *(int *)(in + 4);

    int optr = 0;
    int iptr = 8;

    for (; cnt > 0; --cnt) {
        out.Pointer(optr);
        out.Expand(65000);

        DWORD len = out.Len() - optr;
        int szb = *(DWORD *)(in + iptr);
        if (uncompress(out.Buff<BYTE>() + optr, &len, in + iptr + 4, szb) != Z_OK) {
            out.Clear();
            return 0;
        }

        iptr += szb + 4;
        optr += len;
        out.SetLenNoShrink(optr);
    }

    out.SetLenNoShrink(optr);
    return optr;
}

static void ZL03_Compression(CBuf &out, BYTE *in, int inlen) {
    out.Clear();
    out.Add<uint8_t>('Z');
    out.Add<uint8_t>('L');
    out.Add<uint8_t>('0');
    out.Add<uint8_t>('3');
    out.Add<uint32_t>(0);  // will be updated. blocks count

    int cnt = 0;

    int ptro = out.Pointer();
    int ptri = 0;

    while (ptri < inlen) {
        const int size = (inlen - ptri > 65000) ? 65000 : inlen - ptri;
        DWORD len = compressBound(size);
        out.Pointer(ptro);
        out.Expand(sizeof(DWORD) + len);
        const int res = compress2(out.Buff<BYTE>() + ptro + sizeof(DWORD), &len, in + ptri,
                                  size, Z_BEST_COMPRESSION);
        if (res != Z_OK) {
            ERROR_E;
        }
        *(DWORD *)(out.Buff<BYTE>() + ptro) = len;
        ptro += len + sizeof(DWORD);
        ptri += size;
        out.SetLenNoShrink(ptro);
        ++cnt;
    }
    *(DWORD *)(out.Buff<BYTE>() + 4) = cnt;
}

void CStorageRecordItem::InitBuf(CHeap *heap) {
    ReleaseBuf(heap);
    m_Buf.get_deleter().heap = heap;
    m_Buf.reset(HNew(heap) CDataBuf(heap, m_Type));
}
void CStorageRecordItem::ReleaseBuf([[maybe_unused]] CHeap *heap) {
    m_Buf.reset();
}

DWORD CStorageRecordItem::CalcUniqID(DWORD xi) {
    DWORD x = CalcCRC32_Buf(xi, m_Name.c_str(), m_Name.length() * sizeof(wchar));
    x = CalcCRC32_Buf(x, m_Buf->Get(), m_Buf->Len());
    x = CalcCRC32_Buf(x, &m_Type, sizeof(m_Type));
    return x;
}

void CStorageRecordItem::Save(CBuf &buf, bool compression) {
    ASSERT(m_Buf);
    m_Buf->Compact();
    buf.WStr(m_Name);

    if (compression) {
        buf.Add<uint32_t>(m_Type | ST_COMPRESSED);
        CBuf cb;
        ZL03_Compression(cb, (BYTE *)m_Buf->Get(), m_Buf->Len());

        buf.Add<uint32_t>(cb.Len());
        buf.Add(cb.Get(), cb.Len());
    }
    else {
        buf.Add<uint32_t>(m_Type);
        buf.Add<uint32_t>(m_Buf->Len());
        buf.Add(m_Buf->Get(), m_Buf->Len());
    }
}
bool CStorageRecordItem::Load(CBuf &buf) {
    ASSERT(m_Buf);

    m_Name = buf.WStr();
    m_Type = (EStorageType)buf.Get<DWORD>();
    DWORD sz = buf.Get<DWORD>();

    if (m_Type & ST_COMPRESSED) {
        m_Type = (EStorageType)(m_Type & ~ST_COMPRESSED);
        if (0 == ZL03_UnCompress(*m_Buf, buf.Buff<BYTE>() + buf.Pointer(), sz))
            return false;
        buf.Pointer(buf.Pointer() + sz);
    }
    else {
        m_Buf->Clear();
        m_Buf->Expand(sz);
        buf.Get(m_Buf->Get(), sz);
    }

    return true;
}

void CStorageRecord::AddItem(const CStorageRecordItem &item) {
    m_Items.push_back(item);
}

CStorageRecord::CStorageRecord(const CStorageRecord &rec)
  : m_Heap(rec.m_Heap), m_Name(rec.m_Name), m_Items(rec.m_Items) {
    for (auto &item : m_Items) {
        item.InitBuf(m_Heap);
    }
}

CDataBuf *CStorageRecord::GetBuf(const wchar *column, EStorageType st) {
    for (auto &item : m_Items) {
        if (item.GetName() == column)
            return item.GetBuf(st);
    }
    return NULL;
}

void CStorageRecord::Save(CBuf &buf, bool compression) {
    buf.WStr(m_Name);
    buf.Add<uint32_t>(static_cast<uint32_t>(m_Items.size()));
    for (auto &item : m_Items) {
        item.Save(buf, compression);
    }
}

DWORD CStorageRecord::CalcUniqID(DWORD xi) {
    DWORD x = CalcCRC32_Buf(xi, m_Name.c_str(), m_Name.length() * sizeof(wchar));
    for (auto &item : m_Items) {
        x = item.CalcUniqID(x);
    }
    return x;
}

bool CStorageRecord::Load(CBuf &buf) {
    m_Items.clear();
    m_Name = buf.WStr();
    const DWORD count = buf.Get<DWORD>();
    m_Items.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        m_Items.emplace_back(m_Heap);
        if (!m_Items.back().Load(buf)) {
            m_Items.clear();
            return false;
        }
    }
    return true;
}

CStorage::CStorage(CHeap *heap) : m_Heap(heap) {}

void CStorage::Clear(void) {
    m_Records.clear();
}

void CStorage::AddRecord(const CStorageRecord &sr) {
    m_Records.push_back(sr);
}

void CStorage::DelRecord(const wchar *table) {
    for (size_t i = 0; i < m_Records.size(); ++i) {
        if (m_Records[i].GetName() == table) {
            // Preserve the legacy swap-with-last deletion order.
            if (i + 1 != m_Records.size()) {
                m_Records[i] = std::move(m_Records.back());
            }
            m_Records.pop_back();
            break;
        }
    }
}

bool CStorage::IsTablePresent(const wchar *table) {
    for (const auto &record : m_Records) {
        if (record.GetName() == table) {
            return true;
        }
    }
    return false;
}

CDataBuf *CStorage::GetBuf(const wchar *table, const wchar *column, EStorageType st) {
    for (auto &record : m_Records) {
        if (record.GetName() == table) {
            return record.GetBuf(column, st);
        }
    }
    return NULL;
}

DWORD CStorage::CalcUniqID(void) {
    DWORD x = 0xFFFFFFFF;
    for (auto &record : m_Records) {
        x = record.CalcUniqID(x);
    }
    return x;
}

void CStorage::Save(const wchar *fname, bool compression) {
    CBuf buf;
    Save(buf, compression);
    buf.SaveInFile(fname);

    // CBuf    buf1;
    // CBuf    buf2;

    // ZL03_Compression(buf1, buf);
    // buf1.SaveInFile(CWStr(fname)+L"1");

    // ZL03_UnCompress(buf2, buf1);
    // buf2.SaveInFile(CWStr(fname)+L"2");
}

bool CStorage::Load(const wchar *fname) {
    CBuf buf;
    buf.LoadFromFile(fname);
    return Load(buf);
}

void CStorage::Save(CBuf &buf, bool compression) {
    buf.Clear();
    buf.Add<uint32_t>(0x47525453);
    buf.Add<uint32_t>(compression ? 1 : 0);  // version
    buf.Add<uint32_t>(static_cast<uint32_t>(m_Records.size()));  // records count

    for (auto &record : m_Records) {
        record.Save(buf, false);
    }

    if (compression) {
        CBuf buf2;
        ZL03_Compression(buf2, buf.Buff<BYTE>() + 8, buf.Len() - 8);
        buf.SetLenNoShrink(8);
        buf.Expand(buf2.Len());
        buf.SetLenNoShrink(8 + buf2.Len());
        memcpy(buf.Buff<BYTE>() + 8, buf2.Get(), buf2.Len());
    }
}

bool CStorage::Load(CBuf &buf_in) {
    buf_in.Pointer(0);
    DWORD tag = buf_in.Get<DWORD>();
    if (tag != 0x47525453)
        return false;
    DWORD ver = buf_in.Get<DWORD>();

    if (ver > 1)
        return false;

    CBuf buf2;
    CBuf *buf = &buf_in;

    if (ver == 1) {
        // compression!
        ZL03_UnCompress(buf2, buf_in.Buff<BYTE>() + 8, buf_in.Len() - 8);
        buf = &buf2;
        buf2.Pointer(0);
    }

    m_Records.clear();
    const DWORD count = buf->Get<DWORD>();
    m_Records.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        m_Records.emplace_back(m_Heap);
        if (!m_Records.back().Load(*buf)) {
            m_Records.clear();
            return false;
        }
    }
    return true;
}

void CStorage::StoreBlockPar(const wchar *root, const CBlockPar &bp) {
    DTRACE();

    std::wstring root_name(root);

    CStorageRecord sr(root_name, m_Heap);
    sr.AddItem(CStorageRecordItem(L"0", ST_WCHAR));
    sr.AddItem(CStorageRecordItem(L"1", ST_WCHAR));
    sr.AddItem(CStorageRecordItem(L"2", ST_WCHAR));
    sr.AddItem(CStorageRecordItem(L"3", ST_WCHAR));
    AddRecord(sr);

    CDataBuf *propkey = GetBuf(root, L"0", ST_WCHAR);
    CDataBuf *propval = GetBuf(root, L"1", ST_WCHAR);

    int cnt = bp.ParCount();
    for (int i = 0; i < cnt; ++i) {
        propkey->AddWStr(bp.ParGetName(i));
        propval->AddWStr(bp.ParGet(i));
    }

    int uniq = 0;

    propkey = GetBuf(root, L"2", ST_WCHAR);
    propval = GetBuf(root, L"3", ST_WCHAR);
    cnt = bp.BlockCount();

    std::wstring uniq_s;
    for (int i = 0; i < cnt; ++i) {
        propkey->AddWStr(bp.BlockGetName(i));

        do {
            uniq_s = utils::format(L"%d", uniq);
            uniq++;
        }
        while (IsTablePresent(uniq_s.c_str()));

        propval->AddWStr(uniq_s);
        StoreBlockPar(uniq_s.c_str(), *bp.BlockGet(i));
    }
}

void CStorage::RestoreBlockPar(const wchar *root, CBlockPar &bp) {
    CDataBuf *propkey = GetBuf(root, L"0", ST_WCHAR);
    CDataBuf *propval = GetBuf(root, L"1", ST_WCHAR);

    for (DWORD i = 0; i < propkey->GetArraysCount(); ++i) {
        bp.ParAdd(propkey->GetAsWStr(i), propval->GetAsWStr(i));
    }

    propkey = GetBuf(root, L"2", ST_WCHAR);
    propval = GetBuf(root, L"3", ST_WCHAR);

    for (DWORD i = 0; i < propkey->GetArraysCount(); ++i) {
        CBlockPar *bp1 = bp.BlockAdd(propkey->GetAsWStr(i));
        RestoreBlockPar(propval->GetAsWStr(i).c_str(), *bp1);
    }
}

}  // namespace Base
