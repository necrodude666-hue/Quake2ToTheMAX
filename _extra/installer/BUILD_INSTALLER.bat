@echo off
rem #Quake2ToTheMAX 1.0 - builds the installer Quake2ToTheMAX_1.0_Setup.exe.
rem 1. Build Release in Visual Studio 2005 (quake2.sln): Release\quake2.exe, ref_gl.dll, ref_soft.dll, baseq2\gamex86.dll.
rem    Build Release_SSE too to include Quake2SSE.exe: Release_SSE\Quake2SSE.exe, ref_glsse.dll, ref_softsse.dll,
rem    baseq2\gamex86sse.dll.
rem 2. Install NSIS 3 (free, nsis.sourceforge.io).
rem 3. Unzip Q2TTM_patch320.zip (id Software's 3.20 patch) into this folder, so that patch320\ exists.
rem 4. Double-click this file. The installer is written to this folder.
rem Put the texture pack zip files (BUILD_TEXTURE_ZIPS.bat) next to the installer, or let setup find them.
cd /d "%~dp0"
set NSIS=
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set NSIS="%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "%ProgramFiles%\NSIS\makensis.exe" set NSIS="%ProgramFiles%\NSIS\makensis.exe"
if not defined NSIS ( echo NSIS ^(makensis.exe^) not found. Install NSIS 3 from nsis.sourceforge.io. & pause & exit /b 1 )
if not exist patch320\baseq2\pak1.pak ( echo Missing: patch320\ - unzip Q2TTM_patch320.zip into this folder. & pause & exit /b 1 )
for %%f in (quake2.exe ref_gl.dll ref_soft.dll baseq2\gamex86.dll) do if not exist "..\..\Release\%%f" ( echo Missing: Release\%%f - build Release in Visual Studio first. & pause & exit /b 1 )
set SSE=
if exist "..\..\Release_SSE\Quake2SSE.exe" set SSE=/DRELSSEDIR=..\..\Release_SSE
if not defined SSE echo Release_SSE not found: the installer is built without Quake2SSE.exe.
%NSIS% /DQ2DIR=..\..\Quake2 /DRELDIR=..\..\Release %SSE% Q2TTM.nsi
if errorlevel 1 ( echo ERROR: makensis stopped. & pause & exit /b 1 )
echo.
echo Done: Quake2ToTheMAX_1.0_Setup.exe
pause
