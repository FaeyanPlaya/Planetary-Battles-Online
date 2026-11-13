# Making shader packs

A shader pack changes how a battle looks: its colours, light, glow, fog, sharpness. It is HLSL code (Shader Model 3, DirectX 9) drawn over the battle's 3D picture before the interface comes on top. A pack only draws: the battle never reads what it draws, so every player of a network battle may use a pack of their own or none.

Packs live in the `shaders` folder beside `MatrixGame.exe`, one folder or `.zip` per pack. The game comes with one, Жуть / Spooky (`MatrixGame/shaders/Spooky` in the engine): a night with fog, firelight, glow, shaded corners and the terrain's shadows; its code is an example of everything below. In the game: Settings, Graphics, Shaders opens the packs' list: Shaders on or off, the packs (a click chooses one and switches shaders on, a double click opens its settings), and under them Packs folder (F), Pack settings (S), Cancel (Esc), Apply (A), Done (Enter). A pack's settings are its screens (below) with Packs folder (F), Shader pack list (L), Cancel (Esc), Apply (A), Done (Enter). On top of a settings screen: Back (B or Backspace, a further screen's: one screen back, nothing applied) on the left, the screen's name in two lines, Reset (D: every setting as the author set it, after a question; applied by Apply or Done) on the right; under the rows the chosen row's name in two lines. The window is as tall as the screen allows; more rows scroll with the wheel. Done, Apply and opening a pack's settings apply at once (the battle behind the window draws it; F1 with the interface fixes hides the window to look); Cancel leaves what was applied. A name or a value longer than its place is cut with "...", and said whole under the rows while its row is chosen. The choice is kept in `CFG\shaders.txt`.

## A pack

```
shaders/
  Gloomy/              a folder, or Gloomy.zip with the same files (at its top or in its only folder)
    shaders.txt        what the pack is (required)
    bright.hlsl        a pass's code
    final.hlsl
    common.hlsl        code the passes #include
    noise.png          a texture the passes read
```

Names inside a pack: `/` between folders, case does not matter, no `..`. A pack is at most 2000 files and 64 MB, a file at most 8 MB; a zip is stored or deflated, not encrypted.

## shaders.txt

UTF-8. One `Name=value` a line; `#` and `//` start a comment line.

| Line | Meaning |
| --- | --- |
| `Name=`, `NameEn=` | The pack's name in Russian and English. Each text of the pack may be in one language only: it is shown in both. |
| `Description=`, `DescriptionEn=` | Its description (several lines join). |
| `Author=` | Who made it: the Shaders page shows it. |
| `Setting.<Id>=<values>\|<default>\|<Russian>\|<English>[\|cycle or range]` | A choice on the Shaders page, as a mod's: `Setting.GLOW=0,0.5,1.0,1.5\|1.0\|Свечение\|Glow`. Every pass gets `#define GLOW 1.0` with the person's value. A value is a number or a word (letters, digits, `.`, `-`, `_`). A switch: `On,Off` (shown as Вкл/Выкл in Russian; `On` is 1 and `Off` 0 in the code, `#if RAYS`). The last part says how "<" and ">" go: `cycle` round and round (a few choices: `Setting.STYLE=Soft,Hard\|Soft\|Стиль\|Style\|cycle`), `range` from the first value to the last and no further (the arrow at an end is dimmed); without it, `range` when every value is a number, else `cycle`. |
| `Target.<name>=<size>\|<format>` | A texture a pass draws into: `<size>` a scale of the screen (1, 0.5, 0.25... from 1/16 to 2) or pixels, `<width>x<height>` (1 to 4096: `1x1` for an average of the picture), `<format>` `rgba8` (the default), `rgba16f`, `rg16f` or `r32f` (a card without it gets `rgba8`). It keeps what it was drawn until it is drawn again, the next frame too, and is black when it is made (see History). At most 16; a pass reads those its code names (see the samplers below). |
| `Texture.<name>=<file>\|<clamp or wrap>\|<linear or point>` | An image of the pack the passes read (`.png .dds .bmp .tga .jpg`): noise, a colour table. At most 8. |
| `Pass=<file>\|<target or screen>[\|<entry>]` | A pass: its code, where it draws, its function (`main` by default). In order; the last one draws on the `screen`. At most 100. |
| `Screen=<rows>` | The pack's settings screen: its rows by spaces, a setting's `<Id>`, a note's `<Id>`, `[<screen>]` a row that opens a further screen, `-` an empty row. Without it: all the settings in order. |
| `Screen.<Id>=<Russian>\|<English>\|<rows>` | A further screen: its name (its row's and its title) and its rows, as `Screen=`. At most 64. |
| `Note.<Id>=<Russian>\|<English>` | A line of text on a screen (with an "i" when it has an `Info`). |
| `Profile.<Id>=<Russian>\|<English>\|<Setting>=<value> ...` | Several settings at once (Low, High...): `Profile.LOW=Низкий\|Low\|AO=0 SHADOWS=0`. A screen's row `PROFILE` goes through them with "<" and ">" (the first screen has it first when there is no `Screen=`); it says Свой / Custom when the settings match none. Settings a profile does not name stay as they are. Its "i": `Info.PROFILE=`. |
| `Info.<Id>=<Russian>\|<English>` | What the "i" of a setting or a note says; more lines of it join. |
| `Game.<Name>=On\|Off` | A game setting the pack decides while it draws: `Fog` (the game's own distance fog, which paints the far land in the map's sky colour; `Off` for a pack that makes its own haze), `StencilShadows`, `ProjectedShadows`. Those settings' rows are grey with the pack's value while it is on, as Minecraft's shader packs do; the person's own come back when it is off. |
| `Shadow=<size>\|<distance>[\|<setting>]` | A shadow map: the world's depth as the map light (`SunDir`) sees it, for real shadows and light shafts. `Shadow=2048\|2000\|SHAFTS`: 2048 pixels a side (256 to 4096), reaching 2000 of the world's units from the camera (100 to 20000), drawn only while the setting `SHAFTS` is not `0` or `Off` (without a setting: always). The engine draws the objects and the land once more each frame for it, so it costs: give it a setting, and a profile that turns it off. What casts shadows: what the camera sees (a thing just off the screen casts none). A pass that names `ShadowTex`, `ShadowDepth` or `ShadowLit` reads it (one of its 12 pictures). |
| `World=<file>\|<targets>[\|<setting>]` | The world pass: the pack's own pixel shader draws the world's solid things (the land, the map's objects, robots, buildings, turrets, flyers) into 1 to 4 of its targets, before the game draws its picture. `World=world.hlsl\|WorldN WorldM\|GEOMETRY`: the targets by spaces, each of the screen's size (`1`) and all of one format; drawn only while the setting is not `0` or `Off`. The engine draws the world once more for it (as for `Shadow=`: it costs). See "The world pass" below. |
| `Water=<file>[\|<setting>]` | The water drawn by the pack's own pixel shader in the game's place. `Water=water.hlsl\|WATER`: only while the setting is not `0` or `Off` (else the game's water). See "The water" below. |
| `Effects=<file>[\|<setting>]` | The effects (fire, smoke, explosions, shots, beams, the lights' glows) drawn by the pack's own pixel shader in the game's place. See "The effects" below. |
| `Voxels=<size>[\|<setting>]` | The voxel copy: the whole map as a grid of cubes of `<size>` of the world's units (4 to 64; larger on a map too big for 512 of them a side, see `VoxelOrigin.w`), each holding what stands there. `Voxels=10\|GI`: kept only while the setting `GI` is not `0` or `Off`. In it: the land (solid down from its surface) and what stands on the map (trees, rocks, walls, buildings, turrets: their outer voxels); not robots or flyers (they move: the depth and the shadow map have them). Built once a map: the land at once, the things over the first frames (a couple of milliseconds a frame), then kept up as things are built and destroyed. A pass that names `VoxelAt` or `VoxelTex` reads it (one of its 12 pictures). It takes memory: up to 512 x 512 x 64 bytes, twice (the game's and the card's). |

```
Setting.GLOW=0,0.5,1.0,1.5|1.0|Свечение|Glow
Setting.FOG=0,1,2,3|1|Плотность тумана|Fog density
Note.ABOUT=О наборе|About the pack
Info.GLOW=Насколько светятся яркие места.|How much bright places glow.
Screen=GLOW - [Atmosphere] ABOUT
Screen.Atmosphere=Атмосфера|Atmosphere|FOG
```

A name (`<Id>`, a target's, a texture's) is a name in the code: letters, digits and `_`, not starting with a digit, not one of the engine's (`SceneTex`, `DepthTex`, `PrevTex`, `SolidTex` and the names below), not an HLSL word (`half`, `float`...). A pack with a mistake in its `shaders.txt` is shown grey with the mistake under it and does not draw.

A pass has 16 samplers (Shader Model 3). Four are the engine's (`SceneTex`, `DepthTex`, `PrevTex`, `SolidTex`); the other 12 are the targets and textures that pass's code names, its `#include`d files' too (a name in a comment counts). So a pack may have 16 targets and 8 textures, each pass reading up to 12 of them; a pass that names more is the pack's mistake. The shadow map (`Shadow=`) is one of a pass's 12 when its code reads it.

## A pass

A pixel shader drawn over the whole of its target, one pixel each:

```hlsl
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float3 c = tex2D(SceneTex, uv).rgb;   // the battle's picture
    return float4(lerp(Luma(c).xxx, c, 0.6), 1);
}
```

`uv` is 0..1 over the target (0,0 top left); the same `uv` reads every texture. The engine puts this before each pass's code:

| Name | What |
| --- | --- |
| `sampler2D SceneTex` | The battle's picture as drawn, before the pack: in half floats where the card blends into them (most cards), so what the effects add over a bright place goes past 1 rather than being cut at white (`SceneTex - SolidTex` the same over a day's sky as anywhere); 8 bits a channel on a card that cannot. |
| `sampler2D DepthTex` | Its depth as the card keeps it: 0 at the near plane, 1 far; 1 where nothing was drawn (the sky past the map). Read it with `SceneDepth(uv)`. A card without depth textures (INTZ) gives 1 everywhere. |
| `sampler2D SolidTex` | The picture before the effects were drawn on it (fire, explosions, shots, smoke, sparks): `SceneTex - SolidTex` is what the effects added, the places that glow. The interface's marks on the ground (selection rings, paths, the resources' figures over a base) are drawn after the effects into both, so they are not in the difference. Its alpha: 0 where the game's volumetric shadows fell (robots', buildings', trees'), 1 elsewhere (see `ShadowColor`). |
| `sampler2D PrevTex` | What the previous pass drew (`SceneTex` for the first). |
| `sampler2D <target>` | Each target, by its name (not the one being drawn into). |
| `sampler2D <texture>` | Each texture of the pack. |
| `float4 ScreenSize` | This pass's target: width, height, 1/width, 1/height. |
| `float4 SceneSize` | The screen's: width, height, 1/width, 1/height. |
| `float4 Time` | x: seconds since the pack was turned on, y: the battle's time in seconds (stops in a pause), z: the frame's number, w: seconds since the previous frame (0 on the first, at most 1). |
| `float4 CameraPos` | The camera in the world (z is up). |
| `float4 SunDir` | Towards the sun, length 1: the map's main light, the one the game lights the landscape and casts the robots' shadows by (every map has one, whether or not its sky shows a sun). |
| `float4 SunColor`, `AmbientColor` | The map's light (0..1). |
| `float4 FogColor` | The map's sky and fog colour; w is 1 when the map has fog. |
| `float4 FogRange` | Where the game's fog begins and ends, and the view distance, from the camera (the far view mod moves them). |
| `float4 DepthParams` | The projection's `_33`, `_43`, the near and far planes. |
| `float4 State` | x: 1 in a pause, y: the far view's scale, z: frames drawn since the targets were made (0: they hold nothing yet), w: 1 when `SolidTex` is this frame's picture without the effects. |
| `float4 MapInfo` | x: the water's level (z; waves go a little above and below it), y and z: the map's size in the world (x from 0 to y, y from 0 to z). |
| `float4 SkyHorizon` | The map's sky just above the horizon: its skybox's four sides averaged over that band (rgb; w 1). A map's sky colour (`FogColor`) may be far from it; without a skybox (or a picture not read) it stands in (w 0). The same wherever the camera looks. |
| `float4 PrevCameraPos` | `CameraPos` of the previous frame (with `PrevViewProj`: where a place was on the screen then). |
| `float4x4 ShadowViewProj` | A point of the world into the shadow map (`ShadowPos` does it). |
| `float4 ShadowInfo` | The shadow map: x its pixels a side, y 1 / x, z the world's units a pixel of it takes, w the world's units its depth 0..1 takes; w 0: no map this frame (the pack has no `Shadow=`, its setting is off, the light comes from under the ground, the card gives no depth texture). |
| `float4 VoxelOrigin`, `VoxelScale`, `VoxelExtent` | The voxel copy: its lowest corner in the world (xyz) and a voxel's size (w); a place off that corner into `VoxelTex` (xyz), w 1 when there is a copy, 0 none (no `Voxels=`, its setting off, a card without 3D textures); its size in the world (xyz). |
| `float4 ShadowColor` | What the game's volumetric shadow laid on the picture where `SolidTex.a` is 0: `shadowed = lerp(bare, rgb, a')` with `a' = a * (1 - SolidTex.a)`, so `bare = (shadowed - rgb * a') / (1 - a')`. The map's own shadow colour. A pack can take the shadow out, light the picture its way and lay it again (a night's fog would veil it). Only when `State.w` is 1. |
| `float4x4 View, Proj, ViewProj, InvViewProj, PrevViewProj` | The camera's matrices (row vectors: `mul(float4(p, 1), ViewProj)`); `PrevViewProj` is the previous frame's. |
| `float4 LightCount` | x: how many lights this frame (0 to `MAX_LIGHTS`, 32). |
| `float4 LightPos[32]`, `LightColor[32]`, `LightInfo[32]` | The lights (see Lights): xyz where in the world, w how far it shines; rgb its colour; x its kind (`LIGHT_GLOW`, `LIGHT_FIRE`, `LIGHT_FLASH`, `LIGHT_SHOT`), y a number 0..1 of its own. |
| `float ViewDepth(float d)` | A depth to the distance along the view. |
| `float SceneDepth(float2 uv)` | The depth at a point. |
| `float2 ScenePixel(float2 uv)` | The centre of one screen pixel at `uv`. A pass smaller than the screen should read the depth there: its own pixel's centre lies on the corner of four screen pixels, and which of them the depth gives is left to rounding (lines across the view in what is built from it). `SceneNormal` does it itself. |
| `float3 WorldPos(float2 uv, float d)` | The point of the world seen there. |
| `float Luma(float3 c)` | The brightness of a colour. |
| `float3 SceneNormal(float2 uv)` | Which way the surface seen there faces, in the world (length 1, z up), made from the depth around it: the landscape's slopes, a building's walls and roof. (0, 0, 1) where nothing was drawn. |
| `float3 GeneratedNormal(float2 uv, float strength)` | Generated normals: `SceneNormal` bent by the relief of what is drawn there, the picture's light and dark taken for height (as Minecraft packs' generated normals do from a block's texture; the game draws without shaders, so `SolidTex` stands in for the textures, the game's shadow taken out first). `strength` 1: a moderate relief. Six depth reads and four of `SolidTex` more than `SceneNormal`. |
| `float3 ReliefSlope(float2 uv)` | The relief itself: how that height climbs along the surface per unit of the world (a vector in the surface, towards the brighter; 0 on the sky and across an edge). `dot(ReliefSlope(uv), toLight)` over 0: the slope faces away from that light. For a pass that shades the relief itself (Spooky's `lit.hlsl`). |
| `float SceneHeight(float2 uv)` | The height they read: the picture's brightness there without the game's shadow. |
| `float3 ShadowPos(float3 world)` | Where a point of the world is in the shadow map: xy in `ShadowTex`, z its depth there (0 at the light's eye, 1 the far end). |
| `float ShadowDepth(float2 uv)` | The shadow map there: the depth of the first thing the light meets (1: nothing). `ShadowTex` itself: a point-read depth texture, clamped. |
| `float ShadowLit(float3 world, float bias)` | 1 when the map light reaches the point, 0 when something stands before it. `bias`: how many of the world's units nearer the light the point is taken, so a surface does not shade itself (some of `ShadowInfo.z`: a pixel of the map; more on a slope along the light). 1 outside the map and without one. No branches: fit for a loop (Spooky's `rays.hlsl` asks it 16 times a pixel). For soft edges read it a few times around the point. |
| `float VoxelAt(float3 world)` | What stands at a place of the world by the voxel copy: 0 nothing, 1 the land, 2 an object of the map, 4 a building, 5 a turret (`MAT_LAND`, `MAT_OBJECT`, `MAT_BUILDING`, `MAT_CANNON`). 0 outside the map and without a copy. No branches: fit for a loop. To trace a ray, walk it in steps of about a voxel (`VoxelOrigin.w`) and stop at the first place that is not 0: what is behind the camera or hidden on the screen is there too (light and shade from off the screen, reflections). `VoxelTex` itself: a 3D texture, point-read, a voxel's value its kind / 255. |
| `float3 ScreenPos(float3 p)` | Where a point of the world is on the screen: its `uv` and depth (outside 0..1: off the screen). |

## Lights

Each frame the engine gives a pass up to 32 of the battle's lights the camera may see: the robots' and flyers' own (their jets) first, up to 16, then the strongest near the camera: burning wrecks and buildings, flame throwers' flames, the jets of antigrav chassis and flyers (`LIGHT_FIRE`); explosions, rockets, the machine gun's muzzle (`LIGHT_FLASH`); lasers, the discharger's lightning, plasma (`LIGHT_SHOT`); the repairer's beam (`LIGHT_GLOW`). A light's colour is the game's (it lights the ground with it too); it fades as its effect does. Its number in `LightInfo.y` stays the same while it lives: a fire may flicker by it, each in its own way.

```hlsl
float3 p = WorldPos(uv, d), n = SceneNormal(uv), lit = 0;
for (int i = 0; i < MAX_LIGHTS; i++) {
    if (i >= LightCount.x) break;
    float3 to = LightPos[i].xyz - p;
    float fall = saturate(1 - length(to) / LightPos[i].w);
    lit += LightColor[i].rgb * fall * fall * saturate(dot(n, normalize(to)) * 0.7 + 0.3);
}
```

## History

A target keeps what a pass drew in it until a pass draws in it again: a pass that reads it before that sees the previous frame's. So a pack can carry things over: a picture to smooth (with `PrevViewProj` to find where a point was), a brightness the eye gets used to slowly (a `1x1` target, `Time.w` for how much time went). `State.z` is 0 on the frame the targets are made (the start, a new screen size): they are black then.

```
Target.Eye=1x1|r32f        # how bright the eye thinks the picture is
Target.EyeLast=1x1|r32f
Pass=eye.hlsl|Eye          # reads EyeLast (the previous frame's), moves it towards this frame's brightness
Pass=copy.hlsl|EyeLast     # Eye into EyeLast, for the next frame
```

A pass's code may `#include "file.hlsl"` from the pack. An error in the code is in `logs\test.log` beside the game; the Shaders page shows the first one, and the battle is drawn as without the pack.

## The world pass

The passes above see the finished picture: colours and a depth. The world pass sees the world itself. With a `World=` line the engine draws the land and every object once more, each triangle by the pack's shader, with what the game knows of that surface. Its file has a `main` that gets a `World` and fills the targets, in the line's order:

```hlsl
// World=world.hlsl|WorldN WorldM
void main(World w, out float4 normal, out float4 kind) {
    normal = float4(w.normal * 0.5 + 0.5, 1);
    kind = float4(w.material / 16.0, 0, 0, 1);
}
```

| `World` | |
|---|---|
| `float3 pos` | The place in the world. |
| `float3 normal` | The surface's normal there, of length 1: a model's own (smooth over its rounded parts), the map's at its points for the land (smooth over the dunes), the triangle's where there is neither. |
| `float2 uv`, `float4 tex` | The surface's texture coordinates and its first texture's colour there (`WorldTex`, as the game set it for that draw). |
| `float4 color` | The vertex's colour (the land's baked light; white for the models). |
| `float material` | What the thing is: `MAT_LAND` 1, `MAT_OBJECT` 2 (trees, rocks, walls), `MAT_ROBOT` 3, `MAT_BUILDING` 4, `MAT_CANNON` 5 (turrets), `MAT_FLYER` 6, `MAT_WATER` 7 (the water's surface: its normal the waves', `tex` the map's mask of where the water is). A target the pass did not reach keeps 0 (the sky). |
| `float2 motion` | How far the place moved on the screen since the previous frame, in the screen's uv: it was at this pixel's `uv - motion` (so what a target kept from the previous frame is read there: smoothing over time, motion blur). The camera's movement and the thing's own: each part of a robot, a turret's head, a flyer by where that part was then. Not in it: the bending inside one part (a walking leg's own animation), and the first frame of a thing (0 of its own). Keep it in a target of halves (`rg16f`, `rgba16f`): 8 bits are too coarse for it. |

The settings' `#define`s are there, `PBO_WORLD` is 1, and of the constants `Time`, `CameraPos`, `SunDir`, `SunColor`, `AmbientColor`, `MapInfo`, `View`, `Proj`, `ViewProj` (the others and the pack's pictures belong to the finished picture: not here). The targets are black before it draws; the other passes read them as any target.

What the engine keeps to itself: the first target's alpha is the texture's (the game's alpha test goes by it: leaves stay cut out), so keep what you need in rgb there; the other targets' alpha is the pack's. The game's blending is off while the pass draws (what a model draws twice, an overlay, is written twice, the same). The pass has a depth of its own: it does not change `DepthTex`. The water is drawn last, over the land under it, where the map has it (its mask's alpha); not drawn by it: the effects, the interface's marks. A video card that cannot draw into that many targets at once: the pack draws without the pass (its targets stay black: read them so that black means "no data", as Spooky's `half.hlsl` does).

## The water

With a `Water=` line the pack's shader draws the water instead of the game's own passes: the same surface and its waves, each pixel by the pack's `main`, which returns the colour. The engine then lays the game's distance fog over it (while the game's fog is on) and blends it over the picture by its alpha, as the game blends its water.

```hlsl
// Water=water.hlsl
float4 main(Water w) {
    float3 bottom = tex2D(UnderTex, w.screen + w.normal.xy * 0.01).rgb;   // the bottom, bent by the waves
    float3 c = lerp(bottom, float3(0.02, 0.05, 0.05), saturate(w.depth / 30.0));
    return float4(lerp(c, w.color.rgb, 0.5), w.mask);
}
```

| `Water` | |
|---|---|
| `float3 pos` | The surface's place in the world. |
| `float3 normal` | The waves' normal there, of length 1. |
| `float2 uv` | The water texture's coordinates. |
| `float mask` | How much water the map has there: its shore's mask (0 the land, 1 the water); 1 all over outside the map. |
| `float4 tex`, `float4 refl` | The game's water texture (`WaterTex`) and its reflection (`ReflTex`) as the game reads them. |
| `float3 light` | The game's light on it: the map's water colour and its light by the waves' normal, as the game lights it. |
| `float4 color` | The game's own colour (its texture by its light, its reflection over that) and alpha (`mask`): `return w.color;` draws the water as the game does. |
| `float2 screen` | The pixel's place on the screen (uv): for `UnderTex` and `DepthTex`. |
| `float3 under`, `float depth` | What is under the water seen through this pixel (the bottom, or what stands in the water) and how deep the water is above it (the surface's height less its). |
| `float fog` | How much of the game's fog the engine lays over the colour (0 none). |

`UnderTex` is the picture just before the water (the land, the things, the bottom), `DepthTex` the depth then (`SceneDepth`, `ViewDepth`, `WorldPos` as in a pass); the settings, `PBO_WATER` (1), `Time`, `CameraPos`, `SunDir`, `SunColor`, `AmbientColor`, `FogColor`, `FogRange`, `DepthParams`, `MapInfo`, `View`, `Proj`, `ViewProj`, `InvViewProj`, `ScreenPos`, `Luma` too. The water writes no depth (as the game's).

## The effects

With an `Effects=` line the pack's shader draws every effect's pixels: the game still places them (its vertices, its texture coordinates, its blending, its fog), and works out each effect's colour from its textures as it does (`e.color`); the pack's `main` returns the colour drawn. The engine builds the code for each kind of effect (each way the game combines its textures) the first time one comes; a kind the code cannot do (none in the game now) stays the game's, said once in test.log.

```hlsl
// Effects=effects.hlsl: soft effects, fading where they meet the ground
float4 main(Effect e) {
    if (e.placed < 0.5 || e.facing < 0.5) return e.color;
    float behind = ViewDepth(SceneDepth(e.screen)) - ViewDepth(e.depth);
    return Faded(e.color, saturate(behind / 15.0));
}
```

| `Effect` | |
|---|---|
| `float4 color` | The game's own colour and alpha for this pixel: `return e.color;` draws the effects as the game does. |
| `float4 diffuse` | The vertex's colour (an effect's fading, its tint). |
| `float4 tex`, `float2 uv` | Its first texture's colour and coordinates. |
| `float2 screen`, `float depth` | Its place on the screen (uv) and its depth (as `SceneDepth` gives one). |
| `float placed` | 1; 0 for an effect drawn in the screen's coordinates (`screen` and `depth` mean nothing then). |
| `float facing` | 1 for an effect turned to the camera (fire, smoke, flashes, shots, beams, booms); 0 for one laid in the world: the lights' spots on the land, a factory's capture spots, the rings on the ground and the water. A laid one touches what it lies on all over: fading it by its depth (as above) would wipe it out. |

`Faded(color, k)` gives the colour with `k` of what it lays on the picture (1 all, 0 nothing), whichever way the game lays it: by its alpha (smoke, sparks), or added as it is (fire, flashes). `DepthTex` is the depth before the effects (a copy: `SceneDepth`, `ViewDepth`, `WorldPos`), `SolidTex` the picture before them (heat haze, distortion). The settings, `PBO_EFFECT` (1) and the constants of the water (above) are there. The effects' code is shader model 2 (`ps_2_a` where the card takes it): the game's own vertex processing goes on beside it. Its limits: 512 instructions, no `tex2Dlod`, loops of a fixed count.

## What it costs

Started with `PBO_SHADER_TIMING=1` in its environment, the game writes into test.log, every 120 frames, the video card's milliseconds a frame for each part the pack draws (the shadow map, the world pass, the water, the effects, the pack's passes, the whole frame), from the card's own timestamps. It also logs the memory of the pack's textures when they are made, and how long the voxel copy took to build. Measured on desert1_3e at 1920x1080, Spooky with everything on (the card's milliseconds a frame, steady):

| Part | ms |
|---|---|
| The shadow map (`Shadow=2048\|2000`) | 0.8 |
| The world pass (two targets) | 0.85 |
| The water: the pack's / the game's | 0.27 / 0.06 |
| The effects: the pack's / the game's (as many as there are) | 0.1-0.5 / 0.03-0.35 |
| Spooky's ten passes | 1.3-1.9 |
| The whole frame | 4.4-6 (3.1 without the shadow map and the world pass) |

The shadow map and the world pass cost by the number of things drawn more than by their pixels: a map with thousands of trees and rocks (Osada: 3800 of them seen from high up) pays for each of them again. So the shadow map gets only the things whose ball reaches what it covers, and the world pass leaves out the map's own things (not robots, buildings, turrets or flyers) under 4 pixels across in radius on the screen, and keeps no place a frame ago for them (they stand still: their `motion` is the camera's alone). On Osada, the whole map seen from above, Spooky on its defaults: the frame went from 45 to 35 ms (the shadow map 10 to 3.4 ms, the world pass 15 to 11.4 ms); the camera as the game sets it: from 21.8 to 17.6 ms. The picture is the same.

Memory: the pack's textures 110 MB of the card's at 1920x1080 (134 MB with the water's and the effects' copies); the voxel copy of desert1_3e (496x512x30) 31 MB of the game's memory and 8 MB of the card's. It took 199 ms of building over 91 frames (2 ms a frame: about a second and a half) for the map's 2900 things. The effects' code for a kind of draw compiles on a thread of its own (10-60 ms); meanwhile that kind is drawn as the game draws it.

A part's figure includes the time the card waits for the game inside it. Right after a battle starts and while a picture of the window is taken, a part may show several milliseconds that are such waiting, not drawing.

## What it covers, and not yet

- Drawn after the 3D (landscape, water, robots, buildings, effects) and before the interface (health bars, panels, the cursor), which stay as they are.
- With a pack on, the 3D is not multisampled (the game's antialiasing): a pack smooths edges itself if it wants (FXAA and the like over `SceneTex`).
- Normals: from the depth (`SceneNormal`: each triangle's, for any pack), or the surfaces' own from a world pass (smooth). Neither has a texture's bumps: a pack may add those from the picture (`GeneratedNormal`).
- The world pass draws solid things only, and beside the game's own drawing, not in its place: the picture's colours are still the game's (a pack changes them in its passes). The water and the effects are drawn by the pack's shader in the game's place (`Water=`, `Effects=`); the land, the robots and buildings by the game, their colours changed by the passes. The voxel copy has what stands still: robots and flyers are not in it.
