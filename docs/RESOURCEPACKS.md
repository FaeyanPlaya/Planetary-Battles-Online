# Resource packs

[Русская версия](RESOURCEPACKS.ru.md)

A resource pack replaces the game's pictures and sounds with its own, as a Minecraft resource pack does. It never
changes the game's data, so in a network battle everyone may have their own packs, and nobody checks them.

## Where they are and how they are switched on

- The `resourcepacks` folder beside the game. A pack is a folder or a `.zip`.
- Settings → Graphics → Resource packs → Open. The packs available on the left, those chosen on the right.
  - The top chosen pack covers those under it.
  - Enable or disable a pack: its picture, a double click, or Enable / Disable (A).
  - Change the order: ↑ (PgUp), ↓ (PgDn).
- The choice is saved in `CFG\resourcepacks.txt` and used from the game's next start.
- A `.zip` is unpacked into `Cache\resourcepacks` at the start, and again only when the zip changes.

## What is in a pack

At its top (or in its only folder):

- `pack.txt` (UTF-8, `#` comments):

  ```
  Name=Название
  NameEn=Name
  Description=Описание одной строкой
  DescriptionEn=One line of description
  Author=Author
  ```

- `pack.png`: the pack's picture in the menu (any size, shown 32×32).
- Files by the game's own paths: `Matrix\Robot\...`, `Matrix\IFace\...`, `sound\...`.
- `lang\Rus\` and `lang\Eng\`: what the game takes in one language only (voices). They come before the rest of the
  pack.

A pack may replace pictures (`png`, `jpg`, `jpeg`, `bmp`, `dds`, `tga`) and sounds (`wav`, `ogg`). The game takes
nothing else from it (models, maps, texts, settings).

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

## Reserved

The names `<picture>_n` and `<picture>_s` (`Robot1_n.png`, `Robot1_s.png`) are kept for the normal and specular maps
to come (LabPBR). The game does not take them yet.
