// MatrixGame - licensed under GPLv2 or any later version.
// The EXE is a stub: its WinMain is PBOEngine.dll's (src/MatrixGame.cpp), so the mods' DLLs in the mods folder link
// against the engine by a name that stays whatever the EXE is called. The EXE keeps the resources (the icon, the
// manifest, the Rangers font), which the engine finds through GetModuleHandle(nullptr).
