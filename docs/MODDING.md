# Making mods

Mods live in the `mods` folder beside `MatrixGame.exe`, one folder per mod. The game reads them as it starts: no rebuild of the game is needed for a mod that has only data, and a mod with code is a DLL built against this engine.

## A mod's folder

```
mods/
  mymod/            the folder's name is the mod's id: a-z, 0-9, _ and -
    mod.txt         what the mod is (required)
    data.txt        values laid over the game's data for a battle (a battle mod, optional)
    mymod.dll       its code (optional; Dll= in mod.txt)
    ...             anything else it reads (models, textures)
```

The lobby compares every file of every battle mod in the folder (names and bytes). Every player of a battle needs the same files, or the lobby shows the mod as missing or different and the battle does not start. Mods are never sent over the network: the author sends the folder, and each player checks it and puts it into `mods`.

## mod.txt

UTF-8 (or UTF-16 with a BOM). One `Name=value` a line; `#` and `//` start a comment line.

| Line | Meaning |
| --- | --- |
| `Name=`, `NameEn=` | The mod's name in Russian and English (English falls back to Russian). |
| `Description=`, `DescriptionEn=` | Its description; several lines join, one paragraph each. Shown in the mod's "i" window. |
| `Author=` | Who made it. |
| `Kind=battle` or `Kind=personal` | A battle mod changes the battle; a personal mod changes only what one person sees and how they play (see below). |
| `Default=on` | On for a person who never chose. |
| `Dll=mymod.dll` | Its code, in the folder. Leave it out for a mod of data only. |
| `Dependence=a, b` | Mods it needs: the lobby asks to turn them on. |
| `Conflict=a, b` | Mods it cannot run with: a battle with both does not start. |
| `Key.<Id>=<key>\|<Russian>\|<English>\|<group>` | A key of the mod on the settings' keys page, as `KEY_F2`; English and the group may be left out. The person's choice is kept in `CFG\keys.txt` as `<mod>.<Id>`. |
| `Setting.<Id>=<values, comma-separated>\|<default>\|<Russian>\|<English>` | A choice on the settings' mods page; kept in `CFG\mods.txt` as `<mod>.<Id>`. |
| `Switches=` | Our own mods' switches in the engine (below); not for a new mod. |

## Battle mods and personal mods

In a shared battle every PC computes the whole battle and compares it once a second. A battle mod changes the battle itself, so everybody must have it; the host chooses the battle mods. A personal mod may change the camera, the drawing, the interface and the keys, and may give orders to its person's own side as the person could; it must not change the battle, or that player desyncs at once. The host's lobby switch "Только встроенные личные моды" keeps out personal mods that are not ours (their files are checked against the release's own).

## A mod of data: data.txt

A battle mod may carry `data.txt` in the format of the game's own data (the blocks of `CFG\robots.dat`). For each battle the game lays the `data.txt` of every battle mod that is on over its data, then reads its values again. Before the next battle it takes them off again.

- `Name=value` sets the game's parameter of that name in that block, or adds it.
- `Name { ... }` goes into the game's block of that name, or adds it.
- `ReplaceBlocks=1` in a block: the mod's blocks there take the place of all the game's blocks in it; the game's parameters there stay.
- `%MOD%` in a value is the mod's folder (`%MOD%\head.vo`).

A personal mod's `data.txt` is not read. Only the values the game reads for a battle change: weapons' damages, reach, cooldown and heat, prices, timings, the robots' characteristics, the reinforcements. Two of our mods are only data, and are examples:

- `mods\sr2\data.txt` (Баланс КР2) sets single values of `Weapons`, `Price` and `Chars`:

  ```
  Weapons {
      Cooldown {
          WEAPON_GUN=500
      }
  }
  ```

- `mods\sky\data.txt` (Помощь с небес) replaces the reinforcements' templates (`FlyerOrders { GiveBot { ReplaceBlocks=1 ... } }`). It also sets what a call brings, `CallPoints=150` (the game's is 100). Its self-repairing robots wear `SelfRepairHead=%MOD%\reparator.vo` with the game's texture `SelfRepairHeadTexture=`.

## A mod with code: the DLL

A DLL mod is built against this engine's headers and linked with `PBOEngine.dll`, so it sees the engine's classes and globals directly. One of its sources has:

```cpp
#include "ModApi.hpp"

void init(ModApi::Context &mod) {
    // keys (mod.on_key), settings (mod.setting), log lines (mod.log), the folder (mod.folder)
}

PBO_MOD_ENTRY("mymod", init)
```

`ModApi::Context` (`MatrixGame/src/ModApi.hpp`) gives the mod its id and folder, whether it is on now, its keys and settings, a log line and a key handler. To build one, put it beside ours (`MatrixGame/mods/<id>/<id>.cpp` and `mod.txt`) and add the id to `MATRIXGAME_MODS` in `MatrixGame/CMakeLists.txt`. The build makes `mods/<id>/<id>.dll` beside the EXE.

- **The game's version.** The DLL carries the engine's id (`PBO_ENGINE_ID`), and the game loads it only for the same build. "Built for another version of the game" in `logs\test.log` means: rebuild it for this one. Every release of the game needs its mods rebuilt.
- **Hooks.** The engine asks our mods at the places they change (`MatrixGame/src/ModHooks.hpp`): how a missile turns, what a repair factory does, and so on. Each hook belongs to one of our mods and works while that mod's switch is on (`Switches=`), so a hook is not yet open to a new mod. New hooks are added when a mod needs them: ask for one, naming the place in the engine.
- **Licence.** A DLL built against the engine is a work based on it: it is under the GPL (version 2 or later), as the engine is.

## When something goes wrong

- `logs\test.log` beside the EXE says which mods loaded, which data was laid ("Battle mods' data: ..."), and why a mod was left out.
- A crash that closes the game is written to `logs\crash.txt` beside the EXE: the place as module+offset and the call chain.
