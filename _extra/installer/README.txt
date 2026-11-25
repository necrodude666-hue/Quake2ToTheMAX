#Quake2ToTheMAX - installer and release tools

Q2TTM.nsi               the installer script (NSIS 3, ANSI: Windows 95 to 11), English only
BUILD_INSTALLER.bat     builds Quake2ToTheMAX_1.0_Setup.exe (needs NSIS 3, the Release / Release_SSE builds and
                        patch320\ from Q2TTM_patch320.zip)
BUILD_TEXTURE_ZIPS.bat  run in your Quake 2 folder (with tools\bdgn_packtool.exe): one pak per HD set, written
                        straight into Q2TTM_zip\*.zip; AI 4K as three zips (each under 2 GB)
MAKE_SHA256.bat         writes SHA256SUMS.txt for the setup and the zips in its folder
gfx\                   installer pictures (Brain Drain and #Quake2ToTheMAX logos, game pictures)
tools\bdgn_packtool    merges paks and writes stored zip64 zips (source and exe)
tools\bdgn_unzip       unpacks the texture zips in the installer (KERNEL32 only: runs on the first Windows 95)

The texture zips are "stored" (no compression: DDS and TGA hardly shrink) and zip64. Every file is checked with
CRC-32 when it is unpacked. After a pack is installed, the old split paks of the same set (BDGN 3.60b and older)
are moved to baseq2\textures\_old_split_paks\<set>\ so the set is not loaded twice.

Contributors: Avetar. AI development assistance: Claude and ChatGPT.
Full attribution: CREDITS.md in the source release.
Brain Drain: https://braindrainlan.com/
YouTube: https://www.youtube.com/channel/UCepdDpsXhGYvhMJjzLn0cWg
Facebook: https://www.facebook.com/braindrainlan

3.60i corrected source: rebuild the client and ref_gl, including SSE builds,
before testing the rail impact, item visibility and MAX 4K overlay fixes.
No PAK files or binary-only drivers changed.


OPTIONAL SCI-FI SKIES (25 November 2026)
Put Q2TTM_SciFi_Skies.zip beside Setup or in its optional-pack folder.
The ZIP uses stored entries, supported by the existing Win95 unpacker.
It installs baseq2/env/q2ttm_skies (60 TGA faces and a small catalogue).
It is excluded from the core File /r. Do not add the art to the core source
ZIP or force it onto legacy machines. Download and install it separately.
