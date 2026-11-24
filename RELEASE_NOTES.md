# #Quake2ToTheMAX 1.0

## 3.60i rail wall-smoke correction - 24 November 2026

- A rail wall mark emits eight small smoke puffs in sequence, 180 ms apart, starting after the rail core. All originate at the same impact point and rise vertically; they are no longer spread sideways in parallel.
- Each puff grows to six times its starting diameter over one second, then fades for two seconds. The fixed smoke frame is recentered at the impact, removing its baked displacement. The encoded renderer scale stays within range, so the requested growth is preserved.
- Existing 3D smoke controls, presets, benchmark cases and driver/CPU requirements apply. No additional assets, cvars, renderer-interface fields or effect-pool memory are needed.
- This replaces the preceding twelve-puff/fivefold cloud design. That design's large scale values were clipped by the renderer and did not produce the stated visual size increase.
- Wall/lift vibration with fixed 100 Hz physics and unlimited rendering remains an open issue; the owner reports that a 100 FPS rendering limit avoids it.

## 3.60i robustness review and rail impact smoke - 24 November 2026

- Protocol-34 packets with incomplete headers are rejected before channel acknowledgements change. Valid client/server packet layouts and resource limits are retained.
- Network strings preserve high-bit text bytes and consume the complete field when the 2048-byte local result is truncated, keeping the next field aligned.
- Size buffers reject negative/overflowing reservations. Text append now removes its old terminator before reserving space, avoiding a write before the buffer after an allowed overflow.
- PAK loading checks header/directory reads, directory alignment, file boundaries and entry offsets/lengths, and releases temporary resources on rejection. The 65,536-entry limit and existing search order remain.
- The previous rail cloud design is superseded by the rail wall-smoke correction above, including the renderer scale-clipping correction.
- Reviewed R1Q2, EGL, Yamagi and Q2PRO source. BDGN already has renderer batching/state caching and several server protections; this update adds focused checks without importing a new renderer, protocol extensions or movement rules.
- Validation: 86 message/buffer/header cases, including unchanged channel results for valid packets; the old text-append underwrite is reproduced and the corrected path stays within bounds. Boundary checks accept all 31 installed PAKs and their 39,690 entries. Changed functions pass C89 declaration/syntax checks.
- Build Release | Win32 and Release_SSE | Win32 with Visual Studio 2005 to apply the source update.

## 3.60i camera-step update - 24 November 2026

- Stair smoothing now measures each movement command's own vertical rise. Server platform movement is no longer mistaken for a new stair when rendering between fixed physics ticks.
- The source regression reproduces the old false 8-unit camera step and verifies the correction, including 108 combinations of partial commands, platform heights and command-history wrap. Actual stairs retain their smoothing.
- Movement commands, Pmove calls, final predicted positions, collision and network data are unchanged. Fixed 100 Hz physics, the player's rendering limit and existing presets are retained. The shared client correction applies to all renderers and both executable configurations.
- Build with Visual Studio 2005 using Release | Win32 and Release_SSE | Win32 to use the updated source.
- The owner reports that a 100 FPS limit stops the intermittent wall/lift vibration in the affected session. Investigation continues; the earlier console-based bump-lighting diagnosis was withdrawn because pausing also changed the result.

## 3.60i per-pixel surface stability - 24 November 2026

- Per-pixel material filtering and animated-surface selection were reviewed as separate rendering improvements.
- Per-pixel surface normals now use a filtered height signal at the original material density and symmetric differences. High-resolution colour detail keeps its normal resolution/filtering. The height bias accounts for screen minification so larger material features remain visible at distance.
- Modern world lighting and relief now select the same explicit animation frame as the base world pass. Brush relief uses that brush entity's frame. These passes no longer read the expired stack-local world entity or an unrelated last entity. q2dm1 has no animated texinfo; this is a separate source defect found during the audit.
- Read-only structural checks covered all 31 installed PAKs and 90 original/simplified BSP files, including 2,642 DDS, 10,524 TGA, 6,754 WAL and 11,514 PCX headers. No checked header, mip-bound or PAK/BSP-bound error was found. Every drawn world texture has a replacement in each installed texture set; the 53 omitted HD entries are unused authoring/sky placeholders.
- Validation: C89/source checks, 237 executions of the production animation selector, and compile/link/render tests of the lighting shader on RTX 5060. A synthetic noisy-material test reduced subpixel lighting variation by about 95.6%; this measures shader variation, not game FPS.
- Shared ref_gl source covers both Release and Release_SSE. Legacy binary-only AMD/Rendition renderers retain their paths; modern lighting remains behind its existing CPU/driver gate. No PAK, EXE or DLL bytes changed. No protocol, collision, prediction or model/sound index changes.

**Brain Drain presents #Quake2ToTheMAX 1.0**, built on BDGN 3.60i: a Quake II engine for every PC, from a Pentium
133 to today's computers.

**You need the original Quake II (CD, Steam or GOG).** No game data is included.

[Project website](https://brainspace.braindrainlan.nu/quake2tothemax/index.html) · [Downloads](https://brainspace.braindrainlan.nu/quake2tothemax/download.html) · [ModDB](https://www.moddb.com/mods/quake2tothemax)

## Downloads

| File | What |
|---|---|
| `Quake2ToTheMAX_1.0_Setup.exe` | The game: engine, renderers, drivers, BDGN paks, music, low texture packs. id's 3.20 patch is included and installed when needed. |
| `Q2TTM_Textures_HD_Standard.zip` | HD textures, standard (~1.6 GB) |
| `Q2TTM_Textures_HD_Enhanced.zip` | HD textures, enhanced (~1.6 GB) |
| `Q2TTM_Textures_HD_Enhanced_Less_Grain.zip` | HD textures, enhanced, less grain (~1.6 GB) |
| `Q2TTM_Textures_AI_Stable_Diffusion.zip` | AI (Stable Diffusion) textures (~1.6 GB) |
| `Q2TTM_Textures_AI_4K_part1.zip` to `_part3.zip` | AI 4K textures (~5.1 GB in three parts; download all three) |
| `Q2TTM_Textures_HD_Effects.zip` | HD effects, also in 4K (~335 MB) |
| `Q2TTM_Sounds_HD_44kHz.zip` | 44 kHz sounds (~80 MB) |
| `SHA256SUMS.txt` | Checksums of all files |

Put the zip files you want in the same folder as the setup (or point the setup at them with *Browse...*), then run
the setup.

## Highlights

- Presets measured on real hardware for every graphics card family. *default* is vanilla Quake II 3.21.
- Software, OpenGL, AMD 3DNow!, 3dfx Voodoo 1-5, PowerVR, S3 Virge, Rendition Vérité, SMP OpenGL, two CPUs.
- Low RAM and below 32 MB modes, and the *fastest preset (EXTREME)* toggle.
- HD and AI 4K texture packs, per-pixel lighting, reflective water, bloom, SSAO, shadow volumes, 3D weapon effects.
- Quake2SSE.exe for SSE processors.
- Plays online on 3.20 / 3.21 servers.

## Checking the files

Antivirus programs sometimes flag NSIS installers and patched DLLs by mistake. Compare your download with
`SHA256SUMS.txt`: on Windows 7 and later, run `certutil -hashfile <file> SHA256`.

Source code: this repository (GPL v2). Credits: CREDITS.md.

## 3.60i corrective source update - 23 November 2026

- Railgun impact coordinates are no longer changed by the beam effect. A black burn mark is followed by twelve
  overlapping, larger impact smoke puffs when marks and modern smoke are enabled. The cloud starts after the core,
  grows for one second and fades for two seconds, using the installed effect textures.
- Modern wall relief and per-pixel light overlays use a biased less-or-equal depth test instead of exact depth
  equality. Relief height sampling has uniform control flow, filtered heights and finer ray steps to reduce motion
  shimmer in MAX 4K. Original textures, geometry, collision and old renderers are unchanged.
- Pickup sprites retain their native silhouette without texture-detail reduction or mipmaps. Object distance
  culling applies to cosmetic gibs/debris, never rotating weapons, ammunition or powerups.
- Avetar, Claude and ChatGPT are credited in the game and public documentation.
- Version and archive filename remain 1.0 / BDGN 3.60i at the owner's request. No PAK files changed.
- Validation: changed C functions checked for syntax/C89 declaration placement; relief shader checked on the
  available OpenGL driver. Build Release and Release_SSE in Visual Studio 2005 to apply these source changes.
  The AMD and Rendition binary-only renderers are unchanged: the modern overlay and decal fixes
  concern ref_gl, and the shared client visibility/endpoint fixes concern the rebuilt client.
- Online: client visuals only; no protocol, collision, prediction, game rules or model/sound indices changed.


Contributors include **Avetar**, **Claude** and **ChatGPT**. Full attribution: [CREDITS.md](CREDITS.md).

Brain Drain: [website](https://braindrainlan.com/),
[YouTube](https://www.youtube.com/channel/UCepdDpsXhGYvhMJjzLn0cWg),
[Facebook](https://www.facebook.com/braindrainlan).

## 3.60i follow-up - provisional stair movement

- Camera step smoothing now starts when an unsent/provisional movement command first climbs a step. It records
  the actual command sequence and rejects replays/older steps, so committing that same command does not restart
  the 100 ms smoothing. This corrects a source-level vertical-pop path with fixed physics/packet throttling.
- Movement commands, Pmove calls, final predicted positions, collision and network data are unchanged. The shared
  client change applies to both executable configurations and all renderers, including binary-only legacy ones.
- Validation: C89 syntax/declaration checks and regression cases evaluated from the actual helper's parsed C body:
  first provisional step, replay/commit deduplication, ground/height bounds and command-sequence wrap.

- That camera-step change did not alter preset values, PAK files or VSync. Version and source archive filename remain 1.0 /
  BDGN 3.60i. Required Software assembler objects are explicitly retained by the GitHub ignore rules.


## 3.60i EXTREME profile update - 23 November 2026

- The EXTREME toggle uses the owner's config as the graphics/audio reference: classic low-end sprites, front/back
  enemy views at 6.25 fps, slower far animation, static 2D view weapon, simple world, Duke-style view where the
  renderer supports it, a 60% view area, low textures and 4 kHz PC-speaker sound (8 kHz fallback without the 4 kHz pack).
- Do not copy the owner's resolution, keys, mouse preferences, identity or hardware/CPU detection into the preset.
  The existing half-desktop window, supported 75% render scale and OFF restoration path are retained.
- Weapons, enemies, ammunition, powerups and other gameplay objects remain enabled. Both draw-distance controls,
  object distance and Quake 1 fog are off: that fog otherwise creates an implicit far plane even at draw distance 0.
  Ordinary walls still occlude objects. Cosmetic corpse/gib/effect reductions remain part of the profile.
- Applying the EXTREME menu toggle restarts video and sound together; previously its changed audio backend/rate
  could remain inactive. A missing preset file no longer proceeds to the half-window step or marks the toggle ON.
- Quick: my presets includes EXTREME once on every renderer through the same preset function. Benchmarks keep
  their current output mode; their displayed run count is calculated from the updated plan.
- Validation: C89 checks, eight executions of the parsed production profile across renderer/pack/capability
  combinations, and four actual menu-apply cases for the video/audio restart. Window/OFF logic is unchanged.
- Rebuild both client configurations in Visual Studio 2005 to apply the EXTREME source update.


- The preset benchmark preserves the existing first/baseline case, and its view-area guard accepts EXTREME's
  60% view instead of silently changing it to 100%. Older interrupted benchmark plans are not resumed into this plan.
