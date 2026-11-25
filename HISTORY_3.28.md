# BDGN 3.28 - recovered original patch notes

Source: the project owner supplied the original "Quake 2 BDGN 3.28 patch notes" on 25 November 2026. This corrects the earlier documentation gap for 3.21 to BDGN 3.28. It does not assign every intervening change to a particular unpublished revision.

## Cinematics

An Options-menu choice disables the intro and between-mission movies; cl_hack_nocinematics 1 provides the console equivalent.

## Weapon hand and visibility

Weapon hiding and left-hand drawing were made available directly in Options, without visiting Multiplayer / Player Setup.

## Four sound-quality choices

8 kHz and 44 kHz were added alongside the original 11 and 22 kHz choices. The notes describe 8 kHz for slower machines and 44 kHz for faster machines; higher-quality source samples are needed to benefit from 44 kHz. Merely selecting a higher output rate does not restore missing sound detail.

## Simplified OpenGL textures

The Video menu could disable wall and floor textures. The console alternatives were gl_hack_notextures 1 and the more aggressive gl_hack_noimages 1.

## Zoom

A 2.4x zoom was added, bound to mouse2 and rebindable in Customize Controls, with sensitivity adjusted while zooming. Manual settings were zfov and zsensitivity, with dfov and dsensitivity matching the normal fov and sensitivity.

## Controls and mouse defaults

WASD-style bindings and more Customize Controls entries, including weapons, zoom and grenade, were added. Freelook became the default; legacy bindings and mouse settings were reduced.

## SMP OpenGL

Chris Dohnal's SMP patch was integrated as the SMP OpenGL driver choice. The 3.28 notes explain that the tested 3dfx/minigl path was not exposed as an equivalent menu choice because it showed no benefit in those tests. This describes 3.28, not the later BDGN renderer/SMP menus.

## Historical SMP results and requirements

The original author reported approximately 160 to 220 FPS on a dual Athlon MP 2 GHz and no gain on a dual Pentium III 1 GHz. These are historical results quoted from the recovered notes, not new measurements or a promised gain on other hardware. The notes recommend comparing timedemo 1 / demo1.dm2 before and after.

For that original SMP driver, the notes call for two or more logical processors and Windows NT-family systems, including Hyper-Threading where supported. They warn against that old path on Windows 9x/single-processor configurations and say it is mainly useful when CPU-limited. Later BDGN SMP options have their own capability checks and are documented separately.
