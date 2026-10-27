// MatrixGame - licensed under GPLv2 or any later version.
#include "RandomConfiguration.hpp"

#include "CBlockPar.hpp"
#include "utils.hpp"

#include <filesystem>

namespace RandomConfiguration {

Selection resolve(const Base::CBlockPar &options) {
    const auto count = options.ParCount(L"RandomGenerator");
    if (count == 0) return {};
    if (count != 1) return {random::Mode::ParkMiller, false};
    auto value = utils::trim(options.ParGet(L"RandomGenerator"));
    utils::to_lower(value);
    if (value == L"parkmiller") return {};
    if (value == L"legacycrt") return {random::Mode::LegacyCRT, true};
    return {random::Mode::ParkMiller, false};
}

Selection initialize(unsigned int seed, const Base::CBlockPar &options) {
    const auto selection = resolve(options);
    random::seed(seed, selection.mode);
    return selection;
}

Selection initialize_standalone(unsigned int seed, const wchar_t *path) {
    try {
        Base::CBlockPar options;
        if (std::filesystem::exists(path)) options.LoadFromTextFile(path);
        return initialize(seed, options);
    } catch (const Base::CException &) {
    } catch (const std::exception &) {
    }
    random::seed(seed, random::Mode::ParkMiller);
    return {random::Mode::ParkMiller, false};
}

const char *name(random::Mode mode) {
    return mode == random::Mode::LegacyCRT ? "LegacyCRT" : "ParkMiller";
}

} // namespace RandomConfiguration
