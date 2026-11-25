=====================================================================
  --<# BRAIN DRAIN #>--    #Quake2ToTheMAX 1.0  (BDGN 3.60i)
=====================================================================

INSTALLING #Quake2ToTheMAX 1.0
------------------------------
Run Quake2ToTheMAX_1.0_Setup.exe (Windows 95 to Windows 11). Setup finds your Quake II
(Steam, GOG, the CD installation or the usual folders; you can choose another
folder), adds id Software's 3.20 patch when pak1.pak or pak2.pak is missing,
copies your own quake2.exe and DLLs to Q2TTM_backup and installs the game.
Setup finds the texture pack zip files next to itself or in a Q2TTM_zip folder
(or press Browse...): HD standard, HD enhanced, HD enhanced less grain,
AI (Stable Diffusion), AI 4K (three zip files), HD effects and the 44 kHz
sounds. Each set is one pak file (AI 4K: three parts).

You need the original Quake II (CD, Steam or GOG). None of id Software's game
data is included.

Start the game with Quake2.bat (it starts Cacheman first) or quake2.exe.
The default preset is vanilla Quake II 3.21. Choose a preset for your computer
in Video; Options -> Benchmark -> "Quick: my presets" measures which preset is
fastest on your computer (the log is written to baseq2). Online play works on
3.20 / 3.21 servers.


ABOUT THIS BUILD
----------------
This build is the result of a tweaking competition called #Quake2ToTheMAX.
The goal: squeeze the highest possible Quake II framerate out of old
hardware.

  Competition announcement:
    https://www.youtube.com/watch?v=PldaM3mY_84

  Competition entries:
    https://www.youtube.com/watch?v=4Tb8dIo6Yb4
    https://www.youtube.com/watch?v=bdZTdWNVcHk
    https://www.youtube.com/watch?v=34Dwh-9pA-c

It is based on the BDGN (Brain Drain) Quake II build, version 3.28, made by
Nitton Åttiofyra, which is
based on the id Software Quake II 3.21 source code. This readme describes
everything that was added or changed up to version 3.36; CHANGELOG.md and
README.md describe the rest, up to #Quake2ToTheMAX 1.0.

Developed, tested and measured on: AMD K6-3+, Windows 98, GeForce2 GTS, Detonator
7.76, OpenGL. Benchmark method: "timedemo 1".


CREDITS
-------
  Doogie
  Mr BreakfastPie
  Necrodude - creator of the #Quake2ToTheMAX competition
  The guys at Brain Drain Lan
  ShadowSkill2000, ZSC and infania - for their help
  Nitton Åttiofyra (1984) - author of BDGN 3.28, the base of this build
                 https://www.youtube.com/@Nitton%C3%85ttiofyra
  Chris Dohnal - author of mtgl32.dll (the multithreaded "SMP" OpenGL driver)
  AMD - the 3DNow! renderers ref_glam.dll and ref_swam.dll
  3dfx Interactive - 3dfx MiniGL;  Metabyte - WickedGL;  Techland - S3 Virge OpenGL
  Rendition - ref_v1k;  Outer Technologies - Cacheman
  Testers: Pa1983, Chromatic, Lupusrex and everyone who ran the benchmarks

Quake II is (c) id Software. The engine source code is GPL v2 (LICENSE).
The third-party drivers and renderers belong to their authors.


TESTING
-------
All changes were tested on the real test PC (AMD K6-3+, Windows 98,
GeForce2 GTS). If something misbehaves on your system, turn the option off in
the menu and report it. Always keep a backup of your original ref_glam.dll.


WHERE TO FIND THE OPTIONS
-------------------------
Most new features are in the video menu (Options -> Video -> OpenGL driver
settings). Everything can also be set from the console with the cvars named
below. Cvars marked (saved) are stored in config.cfg.


1. VIDEO DRIVER CHOICES
-----------------------
[3DNow! OpenGL]   Uses ref_glam.dll (AMD's 3DNow!-optimized OpenGL renderer,
                  patched by us, see section 8). Requires a CPU with 3DNow!.
                  If your CPU has no 3DNow!, the normal OpenGL renderer
                  (ref_gl) is used instead and a message is shown.
[3DNow! SMP GL]   ref_glam together with the mtgl32 OpenGL driver (gl_driver
                  mtgl32). Falls back to the normal gl/mtgl32 combination if
                  the CPU lacks 3DNow!.

WHAT IS SMP OPENGL / mtgl32.dll?
    mtgl32.dll, by Chris Dohnal, is a multithreaded OpenGL driver wrapper.
    It sits between the game and the real OpenGL driver (opengl32.dll /
    your graphics driver). Instead of the game's own thread executing every
    OpenGL call, the calls are passed on to a second thread that talks to
    the graphics driver. On a multi-CPU (SMP) or multi-core system the game
    can then work on the next frame while the driver is still busy with the
    last one. "SMP" stands for symmetric multiprocessing, i.e. several CPUs.
    On a single-CPU machine such as a K6-3+ there is no second CPU to use,
    so do not expect a gain there - test with timedemo 1 and compare.
    mtgl32.dll must be placed in the Quake II directory (next to the
    renderer DLLs). The menu entry just sets gl_driver to mtgl32 and
    selects ref_glam as the renderer; you can also select any other OpenGL
    driver DLL with the gl_driver cvar.

Menu rows that ref_glam cannot support are hidden when it is selected.


2. SPEED OPTIONS - GRAPHICS
---------------------------
3DNow! / SSE / MMX optimizations     (cvars: gl_3dnow, gl_sse, gl_mmx)
    Hand-written CPU kernels replace slow C code in the renderer: lightmap
    scaling, dynamic lights, float rounding, colour arrays, model animation
    interpolation, culling of boxes against the view frustum, water waves,
    and texture mipmap/resample (MMX). The program picks the best one your
    CPU supports (SSE, then 3DNow!, then plain C). The results are identical
    to the original code. They are only used if the CPU AND the operating
    system report support. Not available in ref_glam (it has its own).

Batched drawing (fast)               (cvars: gl_fastdraw 3, gl_fastparticles 1)
    Monsters and weapon models are sent to the graphics card in a few big
    batches (triangle strips/fans, glDrawElements) instead of thousands of
    small calls. Particles are drawn with one call. This cuts driver overhead
    on slow CPUs. One menu row switches both on.

Draw distance                        (cvar: gl_drawdist, 0 = off, up to 10)
    Slider. Far right = full distance (off). Moving left makes the game stop
    drawing things beyond a shorter distance (from 4096 units down to about
    160) and hides the cut-off with fog. Faster, but you see less far away.
    The sky box is skipped while this is active.

Fullbright world                     (cvar: gl_fastworld 1)
    Turns off the lightmap texture layer. Walls and floors are drawn without
    shadows/lighting - one texture pass less. Looks flat, but is faster.
    Turns off "single colour textures" and "no texture images" to avoid
    conflicts.

Single colour textures               (cvar: gl_hack_notextures 1)
    Draws the world in flat colours instead of textures.

No texture images                    (cvar: gl_hack_noimages 1)
    Does not load texture images at all (a placeholder texture is used).
    Restarts the renderer when changed. Fastest, ugliest.

8-bit textures                       (cvar: gl_ext_palettedtexture)
    Uses 8-bit palettised textures (less memory).

Sync every frame                     (cvar: gl_finish)
    Waits for the graphics card after each frame. Off is usually faster.

Shadows                              (cvar: gl_shadows)
    Model shadows on/off. Turn off for speed.

Dynamic lights                       (cvar: gl_dynamic, saved)
    Off = no dynamic lighting calculations at all (rocket glows, muzzle
    flashes etc.).


3. SPEED OPTIONS - EFFECTS
--------------------------
Fast effects                         (cvar: cl_fastfx 1)
    Removes particle trails from blaster, hyperblaster, plasma and gibs, and
    the blaster's dynamic light.

No smoke                             (cvar: cl_nosmoke 1)
    Removes smoke/flash puffs from muzzle flashes and bullet impacts, and
    the ricochet sound.

Gibs                                 (cvar: g_gibmode 0/1/2)
    0 = normal. 1 = half as many gibs (head becomes a plain cone).
    2 = no gibs. This is a server setting (game DLL), so it applies to games
    you host.

No flies                             (cvar: g_noflies 1)
    No flies buzzing around corpses (server setting).

The menu row "light/smoke/gibs/flies effects" sets cl_nosmoke, gl_dynamic,
g_noflies and g_gibmode in one go.


4. LOW-QUALITY PAK FILES
------------------------
Place these in your game directory (e.g. baseq2):
    pak6-lowtextures.pak     lower-fidelity textures
    pak7-lowsounds.pak       lower-quality sounds

They are used only when switched on, and then take priority over the normal
paks:
    fs_lowtextures 1         (menu: "low fidelity textures")
    fs_lowsounds 1           (menu: "low quality sounds", restarts sound)
If they are off, the files are ignored. Result: faster loading, less memory.


5. SOUND
--------
s_mixinterval  (default 15)
    Minimum number of milliseconds between sound mixing/submit passes. Higher
    = fewer passes = more CPU left for graphics, but a little more sound
    delay. The included fast config uses 40. On the test PC, switching sound
    off entirely gave about 20 fps, so this is worth experimenting with
    (try 15, 40, 80).

s_mmx  (also set by the MMX menu row)
    Mixes sound with MMX instructions. The output is bit-identical to the
    original code, only faster. Used only if the CPU reports MMX.


6. CONFIG PRESETS
-----------------
Menu row "config preset": keep current / default config / fast config.
    default config  - resets graphics settings using defaultconfig.cfg and a
                      built-in table of stock values.
    fast config    - runs fastconfig.cfg, the tuned maximum-fps settings.
Both finish with vid_restart, wait, snd_restart so everything is applied.
The fast config's settings are now properly SAVED, so they are still active
after you restart the game (earlier versions lost them).

Included example: FastConfig_optimized.cfg - a cleaned-up fast config
without binds, player name or "set adr" lines. Edit to taste.


7. OTHER CHANGES
----------------
- New console banner: --<# BRAIN DRAIN #>--    #Quake2ToTheMAX   v3.36
- vid_ref "glam" is accepted from the console.
- Benchmark tip: use "timedemo 1" and compare the fps before and after each
  change. Each option above was kept only if it helped on the test PC.


8. ref_glam.dll PATCHES
-----------------------
AMD's ref_glam.dll has no source code available, so the options below were
added by patching the DLL directly. Patched file size is larger than the
original (extra code section). Features added to it:
    gl_drawdist       draw distance with fog
    gl_hack_noimages  no texture images
    gl_fastworld      fullbright world
    gl_hack_notextures  single colour textures
Not available in ref_glam: the 3DNow!/SSE/MMX rows and batched drawing
(ref_glam already contains AMD's own 3DNow! code). The effect and sound
cvars work with every renderer.
KEEP A BACKUP of the original ref_glam.dll. If it causes trouble, put the
original back - the game works without the patches.


9. TRIED AND REMOVED
--------------------
Batched world drawing and a distance-saving mode (tried in 3.35) made the
game slower and were removed. Interpolation off (r_lerpmodels 0) gives a
small speed-up but makes animation look bad, so it is not the default.


More detail: see CHANGELOG_BDGN_3.28_to_3.36.txt

  Have fun, and frag fast!
=====================================================================


25 November 2026 - OPTIONAL MODERN EFFECTS AND SKIES
New skybox and matching-light choices: Video -> lighting & shadows.
Standard skybox = original map lighting. Ten sky-only sci-fi environments
are in the separate Q2TTM_SciFi_Skies.zip; extract it to the game folder.
New lighting/material, soft smoke, SSR, SSGI, thin fog and underwater-caustic
options have their own capability checks. Default keeps them off; eligible
MAX Fidelity 4K enables them. Modern graphics + skies is a separate benchmark.
SMP system OpenGL now offers the quality profiles too; unsupported MTGL
shader effects retain the original rendering path. See README.md in the
source package for values, material sidecars, validation and limitations.
