// MatrixGame - licensed under GPLv2 or any later version.
// A build tool, not shipped: writes the source of the EXE's pbo_verified_mods (Mods::verified), the fingerprints of
// the personal mods among our mods of the mods folder as this build made them.
//     ModHash <VerifiedMods.cpp> <mods/id folder>...
// (run again when a mod's DLL or mod.txt changes)
#include "Mods.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "ModHash <out.cpp> <mod folder>...\n";
        return 2;
    }
    std::string list;
    for (int i = 2; i < argc; ++i) {
        const std::filesystem::path folder(argv[i]);
        std::ifstream input(folder / "mod.txt", std::ios::binary);
        if (!input) {
            std::cerr << "ModHash: no mod.txt in " << folder.string() << "\n";
            return 1;
        }
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        const std::string id = Mods::id_of(folder.filename().wstring());
        if (id.empty()) {
            std::cerr << "ModHash: " << folder.string() << " is not a mod's folder name\n";
            return 1;
        }
        if (Mods::parse(Mods::decode(bytes)).kind == Mods::Kind::Personal) list += id + ":" + Mods::folder_hash(folder) + ";";
    }
    const std::string text = "// Written by ModHash at the build: the fingerprints of our personal mods of the mods folder.\n"
                             "extern \"C\" __declspec(dllexport) const char pbo_verified_mods[] = \"" + list + "\";\n";
    std::ofstream(std::filesystem::path(argv[1]), std::ios::binary | std::ios::trunc) << text;
    std::cout << "ModHash: " << list << "\n";
    return 0;
}
