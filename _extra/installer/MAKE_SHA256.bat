@echo off
rem #Quake2ToTheMAX - writes SHA256SUMS.txt for the release files in this folder (the installer and the texture
rem pack zips), in the format of sha256sum ("<hash> *<file>"), to put next to the downloads (GitHub release,
rem ModDB, the web page). Put it in the folder with the files and double-click it.
rem Check a download: certutil -hashfile <file> SHA256  (Windows 7 and later).
setlocal
cd /d "%~dp0"
if exist SHA256SUMS.txt del SHA256SUMS.txt
set BAD=0
for %%f in (*.exe *.zip) do call :hash "%%f"
echo.
type SHA256SUMS.txt
echo.
if not %BAD%==0 ( echo ERROR: %BAD% file^(s^) got no hash - read the certutil message above. & pause & exit /b 1 )
echo Done: SHA256SUMS.txt
pause
exit /b 0

:hash
echo %~1 ...
set H=
rem certutil prints a title line, the hash and a status line; only the hash line has no colon
for /f "delims=" %%h in ('certutil -hashfile %1 SHA256 ^| findstr /v ":"') do if not defined H set "H=%%h"
if not defined H (
  certutil -hashfile %1 SHA256
  set /a BAD+=1
  goto :eof
)
set "H=%H: =%"
echo %H% *%~1>>SHA256SUMS.txt
goto :eof
