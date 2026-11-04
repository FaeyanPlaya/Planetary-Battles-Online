// MatrixGame - licensed under GPLv2 or any later version.
#include "ModHooks.hpp"

namespace ModHooks {

Hooks &hooks() {
    static Hooks set;
    return set;
}

}  // namespace ModHooks
