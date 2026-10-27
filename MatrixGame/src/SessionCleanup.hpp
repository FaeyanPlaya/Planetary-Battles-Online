// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

class CFormMatrixGame;

namespace Session {

// Shared standalone teardown for normal completion and initialization failures.
bool cleanup_standalone(CFormMatrixGame *&form, bool &timer_active) noexcept;

// Attempt every release even when an earlier operation reports an error.
template<class... Actions>
bool cleanup(Actions &&...actions) noexcept {
    bool success = true;
    auto attempt = [&success](auto &&action) {
        try { action(); }
        catch (...) { success = false; }
    };
    (attempt(actions), ...);
    return success;
}

} // namespace Session
