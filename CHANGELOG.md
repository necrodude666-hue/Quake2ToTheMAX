# Changelog

## MAX 4K quality reference - 25 November 2026

The highest preset follows the owner's latest visual reference: 16x anisotropic filtering, trilinear full-detail textures, the installed AI 4K world/effect/mark packs, high bloom and SSAO, four per-pixel lights, material response, full water reflections/interactions, high underwater scattering, high god rays, soft/lit smoke, high SSR/SSGI/fog, 40 debris pieces, maximum lava smoke/bursts and relief depth 3.25. Existing driver/CPU/asset checks still decide which features can run. AI 4K effect textures fall back to standard 4K or HD when the optional set is absent. Artistic sky choice, rail colours and the player's FPS limit remain personal choices.

The preset now **keeps the output resolution, fullscreen/window state and window position**. It no longer switches to the desktop resolution/fullscreen. The benchmark uses the same preset settings and restores its saved settings normally. Original/default and the lower performance presets retain their own designs.

The native context requests up to 8x MSAA, the highest level currently offered by this engine. The existing postprocess scene framebuffer is single-sample and uses FXAA; enabling MSAA does not turn that framebuffer into a multisampled target. Render scale stays at native 100% in this reference so the full modern effect chain remains within its buffer budget at 4K output.


## Water continuity and surface-anchored impact flashes - 25 November 2026

- Underwater caustics and light shafts use stable world coordinates. Moving the camera's 32 x 32 visibility mask by a 128-unit cell no longer restarts or translates the procedural wave pattern.
- Point-source refraction is evaluated at the world sample. The camera's cached direction no longer steers the field, and a timed visibility refresh keeps the selected lamp while swimming in the same water body. Leaving the water, changing maps, changing sky or rewinding time invalidates the selection.
- The large **q2dm1 / The Edge pool leading to the railgun** has an additional fixed overhead source for modern underwater lighting. Its position is `(400, 96, 760)`, above the original water plane at z=440. It contributes only through visible air/water entries, so roofs and walls block it. This is a renderer effect, with no change to the BSP, entities, protocol or original baked lighting. Other maps continue to use their existing visible lamps/sky.
- HD blaster/hyperblaster wall-impact flashes now lie on the hit plane at the mark, with a half-unit depth separation. They no longer move towards the camera. Their dynamic light stays at the original network impact position. Ordinary sprites and the original explosion path retain their previous behaviour; no new effect assets or renderer ABI fields are required.

Controls remain **Video -> effects & water -> Water**: underwater caustics on, underwater light rays high. Console equivalents are `gl_caustics 1` and `gl_water_scattering 2`. MAX 4K selects these where the CPU/active driver permits them. The supplemental pool source is part of those water effects; it does not add a server-side lamp or a visible lamp model. Unsupported/legacy paths still skip the modern water pass.

Validation: 13 C units pass the 32-bit compile check; five shader programs link and seven GPU render checks pass on RTX 5060. The new regression checks cover five mask-grid moves without a phase change, camera-independent point-source direction, actual sprite drawing at 12 wall angles x five camera bases, source-cache lifetime, and air/water/obstruction checks against the retail q2dm1 BSP. The source keeps the existing Visual Studio 2005 build configurations and release layout.


## Native SMP and underwater-light corrections - 25 November 2026

**SMP OpenGL now uses the system OpenGL context on eligible modern CPUs.** Model vertex and normal interpolation runs on up to three CPU workers while every OpenGL call stays on the rendering thread. This replaces the earlier menu-only workaround: that workaround exposed fidelity presets through MTGL but could not execute their shader/FBO effects. Native SMP and ordinary OpenGL now use the same actual driver capability checks and the same High/Ultra/MAX 4K presets. A remembered system-OpenGL MTGL selection migrates before renderer initialization. Legacy CPUs, 3dfx/WickedGL/Glide, Rendition and AMD binary renderers retain their existing paths.

`gl_smp 0/1` disables/enables native CPU pose work (default 0, saved); the SMP OpenGL driver entry sets it to 1 on eligible systems. `gl_smp_stats` reports worker count, prepared poses, worker-executed jobs and reused poses. `bdgn_gl_smp_workers` is a read-only-in-practice runtime report, not a performance setting. Workers start lazily on a multi-processor system, use at most 4 MiB of pose storage, synchronize before the scene ends and join before models/DLLs are freed. Reflection passes never reuse another frame's pose cache. No FPS gain is assumed: compare the native SMP off/on benchmark cases on the intended machine.

Underwater caustics now project through the actual air/water boundary along a refracted sun/lamp direction. A small BSP visibility mask admits light only through exposed water. Moving, softened, distorted patterns replace the previous X/Y stripe pattern; wet receiver checks exclude dry geometry and lava/slime. `gl_water_scattering 0/1/2` adds soft angled underwater light shafts and distance-dependent colour absorption (off/low/high; default off). It is in **Video -> effects & water -> Water** beside underwater caustics. Except for the supplemental q2dm1 pool source described above, maps need an existing visible source above the water for caustics/shafts. MAX Fidelity 4K enables caustics and high scattering where the driver supports them. Standard resets them to off. Standard skybox still means original map lighting.

Blaster HD impact effects and dynamic explosion lights now retain their original contact point; visual sprite pull and large wall-normal fireball offsets no longer move the light source sideways. Rocket, grenade and BFG light origins use the actual impact position. This addresses source-position drift; a bright specular reflection from a different light remains a separate visual effect.

The modern benchmark contains up to **36 cases**, including the new water quality levels and native SMP off/on with the same combined-high settings. Output resolution/window/render scale remain unchanged, and settings restore through the normal benchmark snapshot.

Validation: 13 changed C units pass 32-bit compilation; five shader programs link and six GPU render tests pass on the RTX 5060 driver, including animation, source direction, wet/dry/air/occlusion rejection and exact off restoration. The actual pose-worker code passes 100 frames x 128 models x 1024 vertices with identical positions/normals, 12,800 confirmed worker jobs, reflection/stale-frame rejection and repeated pending-job teardown. Native/legacy routing, real fidelity capability checks and the 36-case benchmark plan pass their regression checks. The source package retains the existing Visual Studio 2005 Win32 build configurations.


## Optional modern effects and sci-fi skies - 25 November 2026

Ten new **sky-only** environments are provided in `Q2TTM_SciFi_Skies.zip`: Ferrum orbit, Crimson nebula, Azure rift, Violet dusk, Golden dawn, Ringworld day, Aurora night, Orbital shipyard, Deep-space fleet and Eclipse. There is no terrain beneath the viewer. All six faces of each environment come from one spherical source and share matching edge samples. The game images are 1024 x 1024 per face; the generated panoramas are not described as native 4K art.

Extract the optional ZIP into the game folder. Its files belong in `baseq2/env/q2ttm_skies/`. The installer can also install it when the ZIP is beside Setup or in the selected optional-pack folder. `BUILD_TEXTURE_ZIPS.bat` can repack a complete installed sky set as a stored ZIP. The core installer excludes that folder. Only the selected six sides load; the previous optional sky textures are released after each live change; an incomplete set falls back to the complete original map sky. This is client-side and leaves server sky/configstrings, maps and protocol 34 unchanged.

**Video -> lighting & shadows -> Sky & atmosphere** selects the sky and its lighting. `cl_skybox 0` is **standard: original sky and original lighting**. Values 1-10 select the new environments in the order above. `gl_sky_lighting 1` applies each new sky's restrained scene brightness/tint; `0` keeps original scene lighting. The multipliers range from 0.76 (night) to 1.18 (day), with a mild colour tint. This is an artistic exposure adjustment over the existing baked lighting, not a rebake of the map. HUD and menu colours stay unchanged. The selected environment also supplies a direction/colour from a bright sun, moon, corona or nebula feature for the existing `gl_sun` and `gl_godrays` effects. Those effects retain their own switches and visibility/occlusion checks.

The following optional effects are off in the default configuration:

| Menu | Cvar / values | Behaviour |
| --- | --- | --- |
| Lighting & shadows | `gl_materials` 0/1 | GGX dynamic-light response using roughness and metallic weight; existing baked light remains. Requires per-pixel lighting for a visible response. |
| Lighting & shadows | `gl_ssgi` 0/1/2 | Spatial screen-space indirect light, 8/16 samples; no temporal history. |
| Lighting & shadows | `gl_volume_fog` 0/1/2 | Thin world-anchored fog with 8/16 bounded scattering samples and up to four dynamic lights. Opacity is capped at 22%. |
| Effects & water, below 3D smoke | `gl_soft_particles` 0/1 | Depth fading of modern smoke puffs at opaque walls, floors and models. Does not replace vanilla particles. |
| Effects & water, below 3D smoke | `gl_smoke_lighting` 0/1 | Coloured dynamic light over the retained neutral smoke lighting. |
| Effects & water, picture effects | `gl_ssr` 0/1/2 | Screen-space reflections on reflective world materials, up to 16/32 ray steps. Off-screen/missed reflections retain the original surface colour. |
| Effects & water, water | `gl_caustics` 0/1 | Soft animated caustics projected along a refracted visible source onto wet world surfaces. |
| Effects & water, water | `gl_water_scattering` 0/1/2 | Underwater light shafts and depth-dependent colour absorption. |

Materials default to matte. Names containing metal, pipe or grate get a conservative metal profile. Optional `baseq2/materials/<texture path>.pbr` files override it: for `textures/e1u1/metal2_1.wal`, create `materials/e1u1/metal2_1.pbr` containing two numbers, e.g. `0.35 0.80` (roughness, metallic weight). Roughness is clamped to 0.08-1 and metallic weight to 0-1. This does not claim that colour textures contain authored PBR maps or normal maps. Metadata is cached per registered image and allocated only when used.

CPU/low-memory checks precede new GL work. Legacy machines do not probe these shaders, load the sky pack or allocate their buffers. Eligible native OpenGL must pass actual shader linking and depth/FBO checks; the menu hides unavailable effects. Buffers are allocated lazily and bounded to 96 MiB for the new environment passes; allocation failure retains the original rendering path. The original pixel-light shader is retained as a fallback if a driver rejects the extended shader. View weapons are drawn after the environment/soft-smoke pass so their depth and shape are excluded from it. The compatible MAX Fidelity 4K preset enables all eight options, with high quality where available. Standard resets them to off and the sky to the map's own.

**SMP OpenGL presets:** eligible modern systems use native system OpenGL plus CPU pose workers, as described above. Fidelity availability follows the real shader/FBO/texture capabilities; the legacy MTGL wrappers do not advertise unavailable modern profiles.

**Video -> benchmarks -> Modern graphics + skies**, or `bdgn_bench_modern [demo1.dm2]`, measures individual new effects off/on/quality, installed skies and two combined cases. With all options and skies available there are 36 cases, three runs each. It retains the output resolution, window size and configured render scale, writes the normal TSV log and restores the saved settings after completion/cancellation. Start from a textured modern configuration. Use a combat demo with modern smoke, reflective surfaces and underwater sections to measure the relevant content; a case with none of that content measures overhead only. Preset benchmarking remains available alongside it.

Validation of this source change: eleven changed C translation units passed a 32-bit TinyCC compile check; all new and modified shader programs compiled and linked on GeForce RTX 5060 / NVIDIA 616.64. GPU draw checks covered neutral standard light, dark/day exposure, exact standard restoration, soft-smoke depth fading and matte reflection rejection. Native regressions passed 40 sky-selection checks, 34 OpenGL/SMP preset checks and five early CPU/driver guard checks. All 60 sky faces passed shared-edge checks. These are targeted code/GPU checks; they do not measure complete-game FPS or replace the owner's normal VS2005 Release/Release_SSE build and gameplay review.


## Historical foundation: BDGN 3.28

The recovered original patch notes document cinematic and weapon-hand options, 8/44 kHz audio, OpenGL texture simplification, 2.4x zoom, WASD/freelook defaults and SMP OpenGL as part of **BDGN 3.28 by Nitton Attiofyra**, before the later Quake2ToTheMAX development. [Read the historical notes, cvars and period SMP results](HISTORY_3.28.md). This corrects the previously incomplete early history; it does not claim these as new 3.60i features.

## 3.60i fixed-physics input correction - 25 November 2026

- The fixed 100 Hz input accumulator now retains the time-weighted input for the remaining milliseconds, rather than a remainder from a rounded command. Releasing a movement key no longer leaves permanent phantom input in the provisional command used between physics ticks.
- The old production function reproduces nonzero forward/side input after two seconds without input. The corrected production function reaches zero. A native Win32 regression covers 107 scenarios, including both axes/signs, varying frame durations, full-range short values, fixed 10 ms commands, mouse angles, button taps and single-use impulses.
- A separate native test uses the unchanged q2dm1 BSP and production Pmove/collision code at the owner's captured location. Phantom provisional input reproduces the measured 1/8-unit lateral displacement; corrected idle input keeps the position stable at every tested duration from 1 to 10 ms.
- Pmove, physics rate, movement rules, collision and protocol 34 are unchanged. No rendering cap or VSync change is imposed. Continuous movement remains unchanged in the regression cases; mixed input is resampled without retaining quantization residue.
- The owner confirmed on 25 November 2026 that the reported vibration is resolved in the rebuilt game. This follows the native input regression and q2dm1 collision reproduction. The previous 100 FPS workaround is no longer required for this corrected issue.

## 3.60i rail wall-smoke correction - 24 November 2026

- A rail wall mark emits eight small smoke puffs in sequence, 180 ms apart, starting after the rail core. All originate at the same impact point and rise vertically; they are no longer spread sideways in parallel.
- Each puff grows to six times its starting diameter over one second, then fades for two seconds. The fixed smoke frame is recentered at the impact, removing its baked displacement. The encoded renderer scale stays within range, so the requested growth is preserved.
- Existing 3D smoke controls, presets, benchmark cases and driver/CPU requirements apply. No additional assets, cvars, renderer-interface fields or effect-pool memory are needed.
- This replaces the preceding twelve-puff/fivefold cloud design. That design's large scale values were clipped by the renderer and did not produce the stated visual size increase.
- Historical status before the input correction: wall/lift vibration was open. The owner confirmed the reported vibration resolved after the 25 November input correction.

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

## #Quake2ToTheMAX 1.0 (BDGN 3.60i), 2026-11

First public release.

- Installer (`Quake2ToTheMAX_1.0_Setup.exe`, Windows 95 to 11). It finds Quake II (Steam, GOG, the CD install,
  the usual folders) and adds id Software's 3.20 patch when pak1.pak or pak2.pak is missing. It backs up your
  quake2.exe and DLLs, and installs the texture packs you choose. It finds the packs next to itself or in a
  Q2TTM_zip folder, or you can use *Browse...*.
- One pak file per texture set. AI 4K comes in three parts, each under 2 GB.
- Quake2SSE.exe with its own SSE renderers and game DLL, next to the normal quake2.exe.
- Duke Nukem 3D mode and the "Duke 3D view (Build)" toggle (Animations menu, under Wolfenstein 3D mode): looking up
  and down moves the horizon instead of tilting the view, as in the Build engine. The crosshair stays on the aim and
  the weapon stays put. OpenGL and Software renderers.
- The installer has two very special offers. Nothing is downloaded or installed.
- fastest preset (EXTREME): the fastest measured preset, in a window half the size of the screen, with render
  scale 75 %.
- Weapons, items and enemies are visible in every preset: no object draw distance and no draw distance in the
  presets.
- Menus: Enter and the new *apply* rows apply changes on the Video sub-pages. Effect pages apply their changes when
  you leave them with Esc.
- AMD ref_glam / ref_swam and Rendition ref_v1k: the PCX loader no longer writes outside the picture. This fixes
  possible crashes with the simplified models. ref_swam gets `r_pagein` (no page-in on low RAM computers).
- The benchmark menu is reduced to *Quick: my presets*. The test menu became *advanced...*. Development tests are
  removed.

## BDGN 3.28 to 3.60 (the road to 1.0)

- 3.28 (Nitton Åttiofyra): cinematic/weapon-hand options, 8/44 kHz audio, OpenGL texture simplification, 2.4x zoom, WASD/freelook and SMP OpenGL; see HISTORY_3.28.md for the original-note summary.
- 3.3x-3.4x: hardware detection, presets per graphics card family, Cacheman, mtgl32 SMP OpenGL, AMD 3DNow!
  renderers.
- 3.5x: Software renderer with id's Pentium assembler again, SMP software rendering, low RAM modes, 2D sprite
  enemies and weapons, HD texture packs, HD and 3D weapon effects, per-pixel lighting, reflective water, bloom,
  SSAO, shadow volumes, AI Stable Diffusion and AI 4K textures, fidelity presets, the in-game benchmark, retro modes
  (Doom, Build, Quake 1, Wolfenstein 3D).
- 3.59-3.60: Rendition and S3 Virge support, PC speaker sound, raw mouse input, console completion and history,
  server security fixes, 3dfx MiniGL fixes, page-in fix for low RAM, measured presets from the testers' machines.

Credits: Avetar; AI development assistance by Claude and ChatGPT. See CREDITS.md.

Brain Drain: [website](https://braindrainlan.com/),
[YouTube](https://www.youtube.com/channel/UCepdDpsXhGYvhMJjzLn0cWg),
[Facebook](https://www.facebook.com/braindrainlan).

- The preset benchmark preserves the existing first/baseline case, and its view-area guard accepts EXTREME's
  60% view instead of silently changing it to 100%. Older interrupted benchmark plans are not resumed into this plan.
