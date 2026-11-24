@echo off
rem #Quake2ToTheMAX - builds the texture pack zip files for the installer.
rem Put this file and bdgn_packtool.exe in your Quake 2 folder (next to quake2.exe) and double-click it.
rem Every HD set is merged into ONE pak (a pak must stay under 2 GB) and written straight into a zip in the
rem folder Q2TTM_zip\. AI 4K becomes three zips, one pak each (GitHub release files must be under 2 GB).
rem The files in baseq2\textures\ are not changed. The zip names have no version number: they stay valid for
rem later releases. Free space needed: about 12 GB (as much as the texture packs take).
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist bdgn_packtool.exe ( echo bdgn_packtool.exe is missing next to this file. & pause & exit /b 1 )
if not exist baseq2\textures ( echo baseq2\textures not found. Put this file in your Quake 2 folder. & pause & exit /b 1 )
if not exist Q2TTM_zip mkdir Q2TTM_zip

call :merge hd-standard              pak8-hd-   pak8-hd-all       2000000000 Q2TTM_Textures_HD_Standard.zip
call :merge hd-enhanced              pak8-hdx-  pak8-hdx-all      2000000000 Q2TTM_Textures_HD_Enhanced.zip
call :merge hd-enhanced-less-grain   pak8-hdx2- pak8-hdx2-all     2000000000 Q2TTM_Textures_HD_Enhanced_Less_Grain.zip
call :merge hd-ai-stable-diffusion   pak8-hdsd- pak8-hdsd-all     2000000000 Q2TTM_Textures_AI_Stable_Diffusion.zip
call :merge hd-ai-4k                 pak8-hd4k- pak8-hd4k-part    1900000000 Q2TTM_Textures_AI_4K_part#.zip

rem The HD effects (normal and 4K) have their own menu choices: they are packed as they are. So are the 44 kHz sounds.
set ARGS=
for %%f in (baseq2\textures\hd-effects\pak8-hd*.pak) do set ARGS=!ARGS! -file baseq2/textures/hd-effects/%%~nxf "%%f"
if defined ARGS (
  bdgn_packtool.exe Q2TTM_zip\Q2TTM_Textures_HD_Effects.zip !ARGS!
  if errorlevel 1 goto failed
)
if exist baseq2\sounds\hd-sounds44\pak7-hd-sounds44.pak (
  bdgn_packtool.exe Q2TTM_zip\Q2TTM_Sounds_HD_44kHz.zip -file baseq2/sounds/hd-sounds44/pak7-hd-sounds44.pak baseq2\sounds\hd-sounds44\pak7-hd-sounds44.pak
  if errorlevel 1 goto failed
)
echo.
echo Done. The zip files are in Q2TTM_zip\
pause
exit /b 0

:merge
rem %1 folder, %2 prefix of the old split paks, %3 name of the new pak (without .pak), %4 max bytes per pak, %5 zip
set LIST=
set N=0
for %%f in (baseq2\textures\%1\%2*.pak) do (
  set NAME=%%~nf
  rem an already merged pak, -all or -partN, is not taken again
  if /i "!NAME:-all=!"=="!NAME!" if /i "!NAME:-part=!"=="!NAME!" ( set LIST=!LIST! "%%f" & set /a N+=1 )
)
if %N%==0 goto merged
echo.
echo === %1: %N% pak files
bdgn_packtool.exe Q2TTM_zip\%5 -merge %4 baseq2/textures/%1/%3 %N% %LIST%
if errorlevel 1 goto failed
exit /b 0

:merged
rem no split paks: the set is already merged (installed by the setup) - pack the merged paks as they are
set ZIP=%5
set M=0
for %%f in (baseq2\textures\%1\%3*.pak) do (
  set PN=%%~nf
  set ZN=!ZIP!
  if not "!ZIP:#=!"=="!ZIP!" set ZN=!ZIP:#=%%~nf!
  if not "!ZIP:#=!"=="!ZIP!" set ZN=!ZN:%3=!
  echo.
  echo === %1: %%~nxf
  bdgn_packtool.exe Q2TTM_zip\!ZN! -file baseq2/textures/%1/%%~nxf "%%f"
  if errorlevel 1 goto failed
  set /a M+=1
)
if %M%==0 echo %1: no pak files, skipped.
exit /b 0

:failed
echo.
echo ERROR: bdgn_packtool stopped. Read the message above.
pause
exit 1
