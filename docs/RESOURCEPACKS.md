# Resource packs

[Русская версия](RESOURCEPACKS.ru.md)

A resource pack replaces the game's pictures and sounds with its own. It never
changes the game's data, so in a network battle everyone may have their own packs, and nobody checks them.

## Where they are and how they are switched on

- The `resourcepacks` folder beside the game. A pack is a folder or a `.zip`.
- Settings → Graphics → Resource packs → Open. The packs available on the left, those chosen on the right.
  - The top chosen pack covers those under it.
  - Enable or disable a pack: its picture, a double click, or Enable / Disable (A).
  - Change the order: ↑ (PgUp), ↓ (PgDn).
- Done saves the choice in `CFG\resourcepacks.txt`. In the main menu and the lobby the game uses it at once (the
  battle loads after them); in a battle, after it, when the game is back at its menu.
- A `.zip` is unpacked into `Cache\resourcepacks` as it is used, and again only when the zip changes.

## What is in a pack

At its top (or in its only folder):

- `pack.txt` (UTF-8, `#` comments):

  ```
  Name=Название
  NameEn=Name
  Description=Описание одной строкой
  DescriptionEn=One line of description
  Author=Author
  Music=Replace
  ```

  `Music=Replace`: the pack's battle tracks instead of the original's (see Music; without it they play with them).

- `pack.png`: the pack's picture in the menu (any size, shown 32×32).
- Files by the game's own paths: `Matrix\Robot\...`, `Matrix\IFace\...`, `sound\...`.
- `lang\Rus\` and `lang\Eng\`: what the game takes in one language only (voices). They come before the rest of the
  pack.

A pack may replace pictures (`png`, `jpg`, `jpeg`, `bmp`, `dds`, `tga`) and sounds (`wav`, `ogg`) and bring battle
music (Music). The game takes nothing else from it (models, maps, texts, settings).

## Picture sizes

- Make a picture the original's size times a power of two: ×2, ×4, ×8, ×16 (or ½, ¼… down to 1/16).
- The land, robots, buildings and effects are drawn at the pack's size. A side over 4096 is halved until it fits. A
  picture with a side over 1024 is kept compressed (DXT) in memory.
- A picture of another size is stretched to the nearest such multiple: 100×100 for a 1024×1024 original becomes
  128×128, 155×104 for a 50×33 one 200×132. A picture whose original's sides are not powers of two is never made
  smaller than the original.
- The game cuts its interface into pieces by pixels and writes its texts into it. So interface pictures of any size
  are made the original's size as they load.
- Robots take their side's colour by their texture's alpha channel. Keep it in your texture.

## Maps for shaders

Beside a picture a pack may have its maps: `<picture>_n`, `<picture>_s`, `<picture>_e`, `<picture>_e_s` (for
`Matrix\Robot\Armor1`: `Armor1_n.png`, `Armor1_s.png`, `Armor1_e.png`, `Armor1_e_s.png`). The game itself never
reads them: only a shader pack's world pass does ([SHADERS.md](SHADERS.md), `World`), and only for what it draws
(the land, the map's things, robots, buildings, turrets, flyers). Without shaders, or with a pack that does not read
them, they cost nothing.

- `_n`, as LabPBR's: red and green the normal's x and y (DirectX's way: green 0 up the picture, 255 down; z from them),
  blue the occlusion (255 none), alpha the height (255 the top).
- `_s`, as LabPBR's: red the smoothness, green F0 (0–229) or a metal (230–255), blue the porosity (0–64) or
  subsurface scattering (65–255), alpha the emission (0–254; 255: none). What glows takes the picture's colour.
- `_e`: what glows over the picture, as OptiFine's: its colour, alpha where (0: nothing).
- `_e_s`: `_e`'s own `_s`: its alpha how strongly `_e` glows (0–254; 255: not at all). Without it `_e` glows whole.

A map is read as it is, of any size up to 4096 a side (the picture's coordinates: a smaller or larger map covers the
same surface), and not stretched or compressed. Only a pack's maps: the game's own robots' `_e` pictures (the enemies'
look of old versions, no longer drawn) are not maps. The game's shader packs read them with their LabPBR materials on (Жуть: Materials: LabPBR; Ванилла +: PBR type:
LabPBR 1.3, as Spooklementary's and I Like Vanilla's own choices): the relief of `_n` under the map's light and the
battle's lights ("Normal map strength", "PBR normals amount") and the glow ("Resource packs' glow"); their generated
normals ("Texture relief") are off then, as with LabPBR in those packs. With them on the packs also light the shine of `_s` (Shine: smoothness and metals; the game's own `_gloss` is not drawn over a picture with an `_s`), `_n`'s occlusion (Map occlusion), the parallax by `_n`'s height (Parallax: Relief shadows, or Shadows and shift: the picture shifts too, on robots, buildings and turrets) and the reflections of smooth surfaces (Surface reflections).
## Music

A pack's `music\BATTLE\*.ogg` and `*.mp3` (Ogg Vorbis, MP3) are battle tracks: they play with the original's and the person's own (the game's `music\BATTLE` folder), all in one list. With `Music=Replace` in its `pack.txt` they play instead of the original's (the person's own stay); `Music=Add` (or nothing) adds them. A pack that replaces them but brings no tracks leaves the original's. Tracks of the same name: the first pack's.

