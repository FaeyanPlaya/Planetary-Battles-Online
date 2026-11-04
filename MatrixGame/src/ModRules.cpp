// MatrixGame - licensed under GPLv2 or any later version.
// Mods' Dependence= and Conflict= (Mods.hpp): what switching one brings and takes, and which cannot be on together.
// Apart from Mods.cpp, which reads the mods folder, so that the settings' model and its tests have them alone.
#include "Mods.hpp"

namespace Mods {
namespace {
const Mod *found(const std::vector<Mod> &mods, std::string_view id) {
    const auto it = std::ranges::find(mods, id, &Mod::id);
    return it != mods.end() ? &*it : nullptr;
}
}  // namespace

std::vector<std::string> toggled(const std::vector<Mod> &mods, std::vector<std::string> on, const std::string &id) {
    const auto has = [&](const std::string &x) { return std::find(on.begin(), on.end(), x) != on.end(); };
    // Off goes what depends on a mod that is not on, until nothing more does.
    const auto drop_orphans = [&] {
        for (bool dropped = true; dropped;) {
            dropped = false;
            for (std::size_t i = 0; i < on.size(); ++i) {
                const Mod *mod = found(mods, on[i]);
                if (!mod || std::all_of(mod->depends.begin(), mod->depends.end(), has)) continue;
                on.erase(on.begin() + std::ptrdiff_t(i));
                dropped = true;
                break;
            }
        }
    };
    if (has(id)) {
        std::erase(on, id);
        drop_orphans();
        return on;
    }
    const auto brought = needs(mods, on, id);
    on.push_back(id);
    on.insert(on.end(), brought.begin(), brought.end());
    drop_orphans();
    return on;
}

std::vector<std::string> needs(const std::vector<Mod> &mods, const std::vector<std::string> &on, const std::string &id) {
    std::vector<std::string> coming{id};
    for (std::size_t i = 0; i < coming.size(); ++i)
        if (const Mod *mod = found(mods, coming[i]))
            for (const auto &need : mod->depends)
                if (found(mods, need) && std::find(coming.begin(), coming.end(), need) == coming.end()) coming.push_back(need);
    coming.erase(coming.begin());
    std::erase_if(coming, [&](const std::string &x) { return std::find(on.begin(), on.end(), x) != on.end(); });
    return coming;
}

std::vector<std::pair<std::string, std::string>> conflicting(const std::vector<Mod> &mods,
                                                             const std::vector<std::string> &on) {
    const auto names = [&](const std::string &a, const std::string &b) {
        const Mod *mod = found(mods, a);
        return mod && std::find(mod->conflicts.begin(), mod->conflicts.end(), b) != mod->conflicts.end();
    };
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < on.size(); ++i)
        for (std::size_t j = i + 1; j < on.size(); ++j)
            if (on[i] != on[j] && (names(on[i], on[j]) || names(on[j], on[i]))) pairs.emplace_back(on[i], on[j]);
    return pairs;
}

}  // namespace Mods
