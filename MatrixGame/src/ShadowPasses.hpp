// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "MatrixConfig.hpp"

// Render passes for the configured shadow kinds. Projected shadows are drawn into the stencil
// buffer and darkened by the same composite step as stencil shadow volumes, so either kind needs
// the stencil pass while each kind draws its own geometry only when enabled.
struct ShadowPasses {
    bool volumes = false;    // Stencil shadow volumes.
    bool projected = false;  // Projected shadows.

    bool stencil_pass() const { return volumes || projected; }
};

inline ShadowPasses shadow_passes(const CMatrixConfig &config) {
    return {config.m_ShowStencilShadows, config.m_ShowProjShadows};
}
