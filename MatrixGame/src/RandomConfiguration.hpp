// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include "random.hpp"

namespace Base { class CBlockPar; }

namespace RandomConfiguration {

struct Selection {
    random::Mode mode = random::Mode::ParkMiller;
    bool valid = true;
};

// Reading options does not change an active battle's random stream.
Selection resolve(const Base::CBlockPar &options);
Selection initialize(unsigned int seed, const Base::CBlockPar &options);
Selection initialize_standalone(unsigned int seed, const wchar_t *path = L"CFG\\standalone.txt");
const char *name(random::Mode mode);

} // namespace RandomConfiguration
