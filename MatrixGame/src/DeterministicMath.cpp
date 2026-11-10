// MatrixGame - licensed under GPLv2 or any later version.
//
// The D3DX math functions the engine calls, defined here so the linker takes them instead of d3dx9_43.dll's.
// D3DX picks its code by the processor (x87, SSE, 3DNow!): an approximate reciprocal square root on one CPU, a
// division on another, so two PCs of a network battle part in the last bits of a robot's heading within seconds.
// These are plain arithmetic in the order written, the same on every processor (sinf and cosf come from the C
// library). The results follow Wine's D3DX, which follows the documented behaviour.
#include <d3dx9math.h>

#include <cmath>

namespace {

float dot3(const D3DXVECTOR3 &a, const D3DXVECTOR3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void multiply(D3DXMATRIX &out, const D3DXMATRIX &a, const D3DXMATRIX &b) {
    D3DXMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    out = r;
}

void identity(D3DXMATRIX &m) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) m.m[i][j] = i == j ? 1.0f : 0.0f;
}

}  // namespace

extern "C" {

D3DXVECTOR2 *WINAPI D3DXVec2Normalize(D3DXVECTOR2 *pout, const D3DXVECTOR2 *pv) {
    const float norm = std::sqrt(pv->x * pv->x + pv->y * pv->y);
    if (norm == 0.0f) {
        pout->x = pout->y = 0.0f;
    }
    else {
        pout->x = pv->x / norm;
        pout->y = pv->y / norm;
    }
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXVec3Normalize(D3DXVECTOR3 *pout, const D3DXVECTOR3 *pv) {
    const float norm = std::sqrt(dot3(*pv, *pv));
    if (norm == 0.0f) {
        pout->x = pout->y = pout->z = 0.0f;
    }
    else {
        const D3DXVECTOR3 v = *pv;
        pout->x = v.x / norm;
        pout->y = v.y / norm;
        pout->z = v.z / norm;
    }
    return pout;
}

D3DXVECTOR2 *WINAPI D3DXVec2TransformCoord(D3DXVECTOR2 *pout, const D3DXVECTOR2 *pv, const D3DXMATRIX *pm) {
    const D3DXVECTOR2 v = *pv;
    const float w = pm->m[0][3] * v.x + pm->m[1][3] * v.y + pm->m[3][3];
    pout->x = (pm->m[0][0] * v.x + pm->m[1][0] * v.y + pm->m[3][0]) / w;
    pout->y = (pm->m[0][1] * v.x + pm->m[1][1] * v.y + pm->m[3][1]) / w;
    return pout;
}

D3DXVECTOR2 *WINAPI D3DXVec2TransformCoordArray(D3DXVECTOR2 *pout, UINT outstride, const D3DXVECTOR2 *pv, UINT vstride,
                                                const D3DXMATRIX *pm, UINT n) {
    for (UINT i = 0; i < n; ++i)
        D3DXVec2TransformCoord(reinterpret_cast<D3DXVECTOR2 *>(reinterpret_cast<char *>(pout) + outstride * i),
                               reinterpret_cast<const D3DXVECTOR2 *>(reinterpret_cast<const char *>(pv) + vstride * i), pm);
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *pout, const D3DXVECTOR3 *pv, const D3DXMATRIX *pm) {
    const D3DXVECTOR3 v = *pv;
    const float w = pm->m[0][3] * v.x + pm->m[1][3] * v.y + pm->m[2][3] * v.z + pm->m[3][3];
    pout->x = (pm->m[0][0] * v.x + pm->m[1][0] * v.y + pm->m[2][0] * v.z + pm->m[3][0]) / w;
    pout->y = (pm->m[0][1] * v.x + pm->m[1][1] * v.y + pm->m[2][1] * v.z + pm->m[3][1]) / w;
    pout->z = (pm->m[0][2] * v.x + pm->m[1][2] * v.y + pm->m[2][2] * v.z + pm->m[3][2]) / w;
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformCoordArray(D3DXVECTOR3 *pout, UINT outstride, const D3DXVECTOR3 *pv, UINT vstride,
                                                const D3DXMATRIX *pm, UINT n) {
    for (UINT i = 0; i < n; ++i)
        D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3 *>(reinterpret_cast<char *>(pout) + outstride * i),
                               reinterpret_cast<const D3DXVECTOR3 *>(reinterpret_cast<const char *>(pv) + vstride * i), pm);
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXVec3TransformNormal(D3DXVECTOR3 *pout, const D3DXVECTOR3 *pv, const D3DXMATRIX *pm) {
    const D3DXVECTOR3 v = *pv;
    pout->x = pm->m[0][0] * v.x + pm->m[1][0] * v.y + pm->m[2][0] * v.z;
    pout->y = pm->m[0][1] * v.x + pm->m[1][1] * v.y + pm->m[2][1] * v.z;
    pout->z = pm->m[0][2] * v.x + pm->m[1][2] * v.y + pm->m[2][2] * v.z;
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *pout, const D3DXVECTOR3 *pv, const D3DVIEWPORT9 *pviewport,
                                    const D3DXMATRIX *pprojection, const D3DXMATRIX *pview, const D3DXMATRIX *pworld) {
    D3DXMATRIX m;
    identity(m);
    if (pworld) multiply(m, m, *pworld);
    if (pview) multiply(m, m, *pview);
    if (pprojection) multiply(m, m, *pprojection);
    D3DXVec3TransformCoord(pout, pv, &m);
    if (pviewport) {
        pout->x = pviewport->X + (1.0f + pout->x) * pviewport->Width / 2.0f;
        pout->y = pviewport->Y + (1.0f - pout->y) * pviewport->Height / 2.0f;
        pout->z = pviewport->MinZ + pout->z * (pviewport->MaxZ - pviewport->MinZ);
    }
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pout, const D3DXMATRIX *pm1, const D3DXMATRIX *pm2) {
    multiply(*pout, *pm1, *pm2);
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationX(D3DXMATRIX *pout, FLOAT angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    identity(*pout);
    pout->m[1][1] = c;
    pout->m[1][2] = s;
    pout->m[2][1] = -s;
    pout->m[2][2] = c;
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationY(D3DXMATRIX *pout, FLOAT angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    identity(*pout);
    pout->m[0][0] = c;
    pout->m[0][2] = -s;
    pout->m[2][0] = s;
    pout->m[2][2] = c;
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pout, FLOAT angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    identity(*pout);
    pout->m[0][0] = c;
    pout->m[0][1] = s;
    pout->m[1][0] = -s;
    pout->m[1][1] = c;
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX *pout, FLOAT yaw, FLOAT pitch, FLOAT roll) {
    const float sroll = std::sin(roll), croll = std::cos(roll);
    const float spitch = std::sin(pitch), cpitch = std::cos(pitch);
    const float syaw = std::sin(yaw), cyaw = std::cos(yaw);
    pout->m[0][0] = sroll * spitch * syaw + croll * cyaw;
    pout->m[0][1] = sroll * cpitch;
    pout->m[0][2] = sroll * spitch * cyaw - croll * syaw;
    pout->m[0][3] = 0.0f;
    pout->m[1][0] = croll * spitch * syaw - sroll * cyaw;
    pout->m[1][1] = croll * cpitch;
    pout->m[1][2] = croll * spitch * cyaw + sroll * syaw;
    pout->m[1][3] = 0.0f;
    pout->m[2][0] = cpitch * syaw;
    pout->m[2][1] = -spitch;
    pout->m[2][2] = cpitch * cyaw;
    pout->m[2][3] = 0.0f;
    pout->m[3][0] = pout->m[3][1] = pout->m[3][2] = 0.0f;
    pout->m[3][3] = 1.0f;
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *pout, FLOAT sx, FLOAT sy, FLOAT sz) {
    identity(*pout);
    pout->m[0][0] = sx;
    pout->m[1][1] = sy;
    pout->m[2][2] = sz;
    return pout;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pout, FLOAT x, FLOAT y, FLOAT z) {
    identity(*pout);
    pout->m[3][0] = x;
    pout->m[3][1] = y;
    pout->m[3][2] = z;
    return pout;
}

// By cofactors; nullptr (pout untouched) when the matrix has no inverse.
D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pout, FLOAT *pdeterminant, const D3DXMATRIX *pm) {
    const D3DXMATRIX &a = *pm;
    float inv[16];
    const float *m = &a.m[0][0];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (pdeterminant) *pdeterminant = det;
    if (det == 0.0f) return nullptr;
    float *out = &pout->m[0][0];
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return pout;
}

D3DXPLANE *WINAPI D3DXPlaneFromPoints(D3DXPLANE *pout, const D3DXVECTOR3 *pv1, const D3DXVECTOR3 *pv2, const D3DXVECTOR3 *pv3) {
    const D3DXVECTOR3 e1(pv2->x - pv1->x, pv2->y - pv1->y, pv2->z - pv1->z);
    const D3DXVECTOR3 e2(pv3->x - pv1->x, pv3->y - pv1->y, pv3->z - pv1->z);
    D3DXVECTOR3 n(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
    D3DXVec3Normalize(&n, &n);
    pout->a = n.x;
    pout->b = n.y;
    pout->c = n.z;
    pout->d = -dot3(*pv1, n);
    return pout;
}

D3DXVECTOR3 *WINAPI D3DXPlaneIntersectLine(D3DXVECTOR3 *pout, const D3DXPLANE *pp, const D3DXVECTOR3 *pv1, const D3DXVECTOR3 *pv2) {
    const D3DXVECTOR3 d(pv2->x - pv1->x, pv2->y - pv1->y, pv2->z - pv1->z);
    const float along = pp->a * d.x + pp->b * d.y + pp->c * d.z;
    if (along == 0.0f) return nullptr;
    const float t = (pp->a * pv1->x + pp->b * pv1->y + pp->c * pv1->z + pp->d) / along;
    pout->x = pv1->x - t * d.x;
    pout->y = pv1->y - t * d.y;
    pout->z = pv1->z - t * d.z;
    return pout;
}

// The ray (from rayposition along raydirection) meets the box (slabs).
BOOL WINAPI D3DXBoxBoundProbe(const D3DXVECTOR3 *pmin, const D3DXVECTOR3 *pmax, const D3DXVECTOR3 *prayposition,
                              const D3DXVECTOR3 *praydirection) {
    float div = 1.0f / praydirection->x, tmin, tmax;
    if (div >= 0.0f) {
        tmin = (pmin->x - prayposition->x) * div;
        tmax = (pmax->x - prayposition->x) * div;
    }
    else {
        tmin = (pmax->x - prayposition->x) * div;
        tmax = (pmin->x - prayposition->x) * div;
    }
    if (tmax < 0.0f) return FALSE;
    div = 1.0f / praydirection->y;
    float ymin, ymax;
    if (div >= 0.0f) {
        ymin = (pmin->y - prayposition->y) * div;
        ymax = (pmax->y - prayposition->y) * div;
    }
    else {
        ymin = (pmax->y - prayposition->y) * div;
        ymax = (pmin->y - prayposition->y) * div;
    }
    if (ymax < 0.0f || tmin > ymax || ymin > tmax) return FALSE;
    if (ymin > tmin) tmin = ymin;
    if (ymax < tmax) tmax = ymax;
    div = 1.0f / praydirection->z;
    float zmin, zmax;
    if (div >= 0.0f) {
        zmin = (pmin->z - prayposition->z) * div;
        zmax = (pmax->z - prayposition->z) * div;
    }
    else {
        zmin = (pmax->z - prayposition->z) * div;
        zmax = (pmin->z - prayposition->z) * div;
    }
    if (zmax < 0.0f || tmin > zmax || zmin > tmax) return FALSE;
    return TRUE;
}

// The Windows C runtime's acos, atan, tan and _hypot take an FMA path on processors that have it: these are made of
// the x87 functions the rest of the engine uses (atan2, sin, cos, sqrt), as on every processor.
double __cdecl atan(double x) { return std::atan2(x, 1.0); }
double __cdecl acos(double x) { return std::atan2(std::sqrt((1.0 - x) * (1.0 + x)), x); }
double __cdecl tan(double x) { return std::sin(x) / std::cos(x); }
double __cdecl _hypot(double x, double y) { return std::sqrt(x * x + y * y); }

}  // extern "C"
