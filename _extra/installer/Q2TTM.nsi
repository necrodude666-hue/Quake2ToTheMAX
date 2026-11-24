; #Quake2ToTheMAX - installer (English only, owner 2026-11-22) (NSIS 3, ANSI target: Windows 95, 98, ME, NT 4, 2000, XP, Vista, 7, 8, 10, 11)
; Brain Drain presents #Quake2ToTheMAX.
;
; Build: BUILD_INSTALLER.bat (or: makensis /DQ2DIR=..\..\Quake2 /DRELDIR=..\..\Release Q2TTM.nsi)
;   Q2DIR   the release's Quake2\ folder (renderers, drivers, Cacheman, Quake2.bat, baseq2\ paks, music, sounds, low
;           texture packs, HD effects)
;   RELDIR  the Visual Studio Release folder: quake2.exe, ref_gl.dll, ref_soft.dll, baseq2\gamex86.dll
;   RELSSEDIR (optional) the Release_SSE folder: Quake2SSE.exe, ref_glsse.dll, ref_softsse.dll, baseq2\gamex86sse.dll
; The texture packs are not inside the installer (several GB): they are the zip files next to it, made by
; BUILD_TEXTURE_ZIPS.bat. The installer offers the ones it finds next to itself.

Unicode false
SetCompressor /SOLID lzma
SetCompressorDictSize 32
RequestExecutionLevel admin
ManifestSupportedOS all

!ifndef Q2DIR
  !define Q2DIR "..\..\Quake2"
!endif
!ifndef RELDIR
  !define RELDIR "..\..\Release"
!endif
!define VERSION "1.0"
!define BDGN "3.60i"
!define PRODUCT "#Quake2ToTheMAX ${VERSION}"

Name "${PRODUCT}"
OutFile "Quake2ToTheMAX_${VERSION}_Setup.exe"
BrandingText "Brain Drain - #Quake2ToTheMAX ${VERSION} (BDGN ${BDGN})"
InstallDir "C:\Quake2"
ShowInstDetails show
XPStyle on

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "#Quake2ToTheMAX"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "#Quake2ToTheMAX ${VERSION} setup (BDGN ${BDGN})"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "CompanyName" "Brain Drain"
VIAddVersionKey /LANG=1033 "LegalCopyright" "#Quake2ToTheMAX and Brain Drain (c) Brain Drain. Quake II (c) id Software."

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

!define MUI_ICON "gfx\icon.ico"
!define MUI_ABORTWARNING
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "gfx\header.bmp"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEFINISHPAGE_BITMAP "gfx\welcome.bmp"
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_BGCOLOR "FFFFFF"

; ---------------------------------------------------------------- pages
!define MUI_WELCOMEPAGE_TITLE "$(TXT_WELCOME_TITLE)"
!define MUI_WELCOMEPAGE_TEXT "$(TXT_WELCOME_TEXT)"
!insertmacro MUI_PAGE_WELCOME
Page custom AboutPage
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(TXT_DIR_TOP)"
!insertmacro MUI_PAGE_DIRECTORY
Page custom TexPage TexPageLeave
; the fake "special offers" (offers.nsh, a joke: nothing is downloaded or installed)
Page custom OfferBaskPage OfferLeave
Page custom FakeDLPage FakeDLLeave
Page custom OfferYahootPage OfferLeave
Page custom FakeDLPage FakeDLLeave
!define MUI_PAGE_CUSTOMFUNCTION_PRE ComponentsPre
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishShow
!define MUI_FINISHPAGE_TITLE "$(TXT_FINISH_TITLE)"
!define MUI_FINISHPAGE_TEXT "$(TXT_FINISH_TEXT)"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "$(TXT_RUN)"
!define MUI_FINISHPAGE_RUN_FUNCTION RunGame
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\#Quake2ToTheMAX Readme.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "$(TXT_README)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
ReserveFile "gfx\splash.bmp"
ReserveFile "gfx\about.bmp"
ReserveFile "gfx\finish.bmp"
ReserveFile "gfx\offer_bask.bmp"
ReserveFile "gfx\offer_yahoot.bmp"
ReserveFile /plugin AdvSplash.dll

; ---------------------------------------------------------------- texts
LangString TXT_WELCOME_TITLE ${LANG_ENGLISH} "Brain Drain presents #Quake2ToTheMAX ${VERSION}"
LangString TXT_WELCOME_TEXT ${LANG_ENGLISH} "Quake II from the Pentium 133 to today's computers: every frame per second that can be found, presets for your graphics card, retro modes and HD texture packs.$\r$\n$\r$\nSetup finds your Quake II, adds id Software's 3.20 patch if it is missing, installs #Quake2ToTheMAX and the texture packs you choose. It still plays online with 3.20 / 3.21 servers.$\r$\n$\r$\nYour own quake2.exe and DLLs are copied to Q2TTM_backup first."
LangString TXT_ABOUT_TITLE ${LANG_ENGLISH} "#Quake2ToTheMAX"
LangString TXT_ABOUT_SUB ${LANG_ENGLISH} "Brain Drain presents"
LangString TXT_ABOUT_TEXT ${LANG_ENGLISH} "Default is vanilla Quake II 3.21. Pick a preset in Video for your computer: Fast, Faster, Ultra Fast, PSYCHO Fast (all in) or MAX Fidelity. Options -> Benchmark -> Quick: my presets measures which one is fastest on your computer."
LangString TXT_DIR_TOP ${LANG_ENGLISH} "Setup looked for Quake II (Steam, GOG, the CD installation and the usual folders). Check the folder: it is the one with quake2.exe and baseq2\pak0.pak. #Quake2ToTheMAX is installed there."
LangString TXT_NOQ2 ${LANG_ENGLISH} "There is no baseq2\pak0.pak in this folder, so Quake II does not seem to be installed here.$\r$\n$\r$\nInstall here anyway?"
LangString TXT_ZIPMISSING ${LANG_ENGLISH} " (zip not found)"
LangString TXT_TEX_TITLE ${LANG_ENGLISH} "Texture packs"
LangString TXT_TEX_SUB ${LANG_ENGLISH} "Where are the texture pack zip files (Q2TTM_Textures_*.zip)?"
LangString TXT_TEX_FOUND ${LANG_ENGLISH} "Found $TexCount of 7 packs in:"
LangString TXT_TEX_NONE ${LANG_ENGLISH} "No texture packs found next to setup or in a Q2TTM_zip folder. Press Browse to find the folder with the zip files, or Next to install without texture packs."
LangString TXT_TEX_BROWSE ${LANG_ENGLISH} "Browse..."
LangString TXT_TEX_PICK ${LANG_ENGLISH} "Choose the folder with the texture pack zip files"
LangString TXT_TEX_NOZIP ${LANG_ENGLISH} "There are no Q2TTM texture pack zip files in that folder."
LangString TXT_UNZIPFAIL ${LANG_ENGLISH} "The texture pack could not be installed:"
LangString TXT_FINISH_TITLE ${LANG_ENGLISH} "#Quake2ToTheMAX ${VERSION} is installed"
LangString TXT_FINISH_TEXT ${LANG_ENGLISH} "Start the game with Quake2.bat (it starts Cacheman first) or quake2.exe.$\r$\n$\r$\nYour original files are in Q2TTM_backup."
LangString TXT_RUN ${LANG_ENGLISH} "Start #Quake2ToTheMAX"
LangString TXT_README ${LANG_ENGLISH} "Show the readme"

LangString SEC_PATCH_NAME ${LANG_ENGLISH} "id Software Quake II 3.20 patch"
LangString SEC_SSE_NAME ${LANG_ENGLISH} "SSE version (Quake2SSE.exe)"
LangString DESC_SSE ${LANG_ENGLISH} "Quake2SSE.exe with SSE renderers and game DLL, about 2 % faster on Pentium III / Athlon XP and newer. Start it with Quake2SSE.bat. Chosen automatically when the processor has SSE."
LangString SEC_TEX_NAME ${LANG_ENGLISH} "Texture packs"
LangString DESC_CORE ${LANG_ENGLISH} "The game: quake2.exe, the renderers and drivers (OpenGL, software, 3DNow!, 3dfx, PowerVR, S3, Rendition), the BDGN paks, music, low sound packs and low texture packs."
LangString DESC_PATCH ${LANG_ENGLISH} "id Software's Quake II 3.20 patch: pak1.pak, pak2.pak, the VWep player models, CTF and the mission pack game DLLs. Chosen automatically when pak1.pak or pak2.pak is missing."
LangString DESC_TEX ${LANG_ENGLISH} "HD texture, effect and sound packs (from the zip files in the folder chosen on the previous page). Choose the set in the game: Video -> picture & textures -> HD textures."
LangString DESC_HD ${LANG_ENGLISH} "HD standard (about 1.6 GB): upscaled originals."
LangString DESC_HDX ${LANG_ENGLISH} "HD enhanced (about 1.6 GB): sharper upscale."
LangString DESC_HDX2 ${LANG_ENGLISH} "HD enhanced, less grain (about 1.6 GB)."
LangString DESC_HDSD ${LANG_ENGLISH} "AI (Stable Diffusion) (about 1.6 GB): photo-realistic detail."
LangString DESC_HD4K ${LANG_ENGLISH} "AI 4K (about 5.2 GB, DDS): for the strongest graphics cards (preset Ultra High Fidelity - 4K)."
LangString DESC_FX4K ${LANG_ENGLISH} "HD effects (about 335 MB): HD sprites, modern 3D weapon effects, marks and gore, also in 4K. Not on legacy computers."
LangString DESC_SND ${LANG_ENGLISH} "44.1 kHz 16-bit sounds (about 80 MB), used at sound quality 44 kHz."

; ---------------------------------------------------------------- sections
Var TexFailed
Var ZipDir
Var TexCount
Var TexLabel
Var TexPath
!include "offers.nsh"

Section "-backup"
	SetOutPath "$INSTDIR"
	CreateDirectory "$INSTDIR\Q2TTM_backup"
	CreateDirectory "$INSTDIR\Q2TTM_backup\baseq2"
	!macro BACKUP f
		${If} ${FileExists} "$INSTDIR\${f}"
		${AndIfNot} ${FileExists} "$INSTDIR\Q2TTM_backup\${f}"
			CopyFiles /SILENT "$INSTDIR\${f}" "$INSTDIR\Q2TTM_backup\${f}"
		${EndIf}
	!macroend
	!insertmacro BACKUP "quake2.exe"
	!insertmacro BACKUP "ref_gl.dll"
	!insertmacro BACKUP "ref_soft.dll"
	!insertmacro BACKUP "baseq2\gamex86.dll"
SectionEnd

Section "$(SEC_PATCH_NAME)" SEC_PATCH
	DetailPrint "id Software Quake II 3.20"
	SetOutPath "$INSTDIR"
	File /r "patch320\*.*"
SectionEnd

Section "#Quake2ToTheMAX ${VERSION}" SEC_CORE
	SectionIn RO
	SetOutPath "$INSTDIR"
	; the HD effects and the 44 kHz sounds are optional zips (Texture packs), not part of the game
	File /r /x "hd-effects" /x "hd-sounds44" "${Q2DIR}\*.*"
	File "${RELDIR}\quake2.exe"
	File "${RELDIR}\ref_gl.dll"
	File "${RELDIR}\ref_soft.dll"
	File "/oname=#Quake2ToTheMAX Readme.txt" "..\#Quake2ToTheMAX Readme.txt"
	SetOutPath "$INSTDIR\baseq2"
	File "${RELDIR}\baseq2\gamex86.dll"
	SetOutPath "$INSTDIR"
	WriteRegStr HKLM "Software\Brain Drain\#Quake2ToTheMAX" "Path" "$INSTDIR"
	WriteRegStr HKLM "Software\Brain Drain\#Quake2ToTheMAX" "Version" "${VERSION}"
SectionEnd


!ifdef RELSSEDIR
; Quake2SSE.exe (/arch:SSE) takes ref_glsse.dll, ref_softsse.dll and gamex86sse.dll, else the normal DLLs. Chosen when
; Windows reports SSE (IsProcessorFeaturePresent; Windows 95 does not have it and does not save SSE registers anyway)
Section /o "$(SEC_SSE_NAME)" SEC_SSE
	SetOutPath "$INSTDIR"
	File "${RELSSEDIR}\Quake2SSE.exe"
	File "${RELSSEDIR}\ref_glsse.dll"
	File "${RELSSEDIR}\ref_softsse.dll"
	SetOutPath "$INSTDIR\baseq2"
	File "${RELSSEDIR}\baseq2\gamex86sse.dll"
	SetOutPath "$INSTDIR"
	FileOpen $0 "$INSTDIR\Quake2SSE.bat" w
	FileWrite $0 "echo off$\r$\nCachem.exe -quiet 20$\r$\n$\r$\nQuake2SSE.exe$\r$\n"
	FileClose $0
SectionEnd
!endif

; a texture pack: unzip it, then move the old split paks of the same set (v3.60b and earlier) aside, so the set is not
; loaded twice. They go to baseq2\textures\_old_split_paks\<set>\ (a rename on the same disk: no extra space).
!macro TEXPACK SEC NAME ZIP FOLDER PREFIX
Section /o "${NAME}" ${SEC}
	StrCpy $0 "${ZIP}"
	StrCpy $1 "${FOLDER}"
	StrCpy $2 "${PREFIX}"
	Call InstallTexturePack
SectionEnd
!macroend

SectionGroup /e "$(SEC_TEX_NAME)" SEC_TEX
	!insertmacro TEXPACK SEC_HD   "HD standard"              "Q2TTM_Textures_HD_Standard.zip"             "hd-standard"            "pak8-hd-"
	!insertmacro TEXPACK SEC_HDX  "HD enhanced"              "Q2TTM_Textures_HD_Enhanced.zip"             "hd-enhanced"            "pak8-hdx-"
	!insertmacro TEXPACK SEC_HDX2 "HD enhanced, less grain"  "Q2TTM_Textures_HD_Enhanced_Less_Grain.zip"  "hd-enhanced-less-grain" "pak8-hdx2-"
	!insertmacro TEXPACK SEC_HDSD "AI (Stable Diffusion)"    "Q2TTM_Textures_AI_Stable_Diffusion.zip"     "hd-ai-stable-diffusion" "pak8-hdsd-"
	!insertmacro TEXPACK SEC_HD4K "AI 4K"                    "Q2TTM_Textures_AI_4K_part1.zip"             "hd-ai-4k"               "pak8-hd4k-"
	!insertmacro TEXPACK SEC_FX4K "HD effects"               "Q2TTM_Textures_HD_Effects.zip"              "hd-effects"             ""
	!insertmacro TEXPACK SEC_SND  "HD sounds 44 kHz"         "Q2TTM_Sounds_HD_44kHz.zip"                  "hd-sounds44"            ""
SectionGroupEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE} $(DESC_CORE)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_PATCH} $(DESC_PATCH)
!ifdef RELSSEDIR
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_SSE} $(DESC_SSE)
!endif
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_TEX} $(DESC_TEX)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HD} $(DESC_HD)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HDX} $(DESC_HDX)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HDX2} $(DESC_HDX2)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HDSD} $(DESC_HDSD)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HD4K} $(DESC_HD4K)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_FX4K} $(DESC_FX4K)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_SND} $(DESC_SND)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------- functions
; $0 zip, $1 set folder, $2 prefix of the old split paks ("" = keep them)
Function InstallTexturePack
	${IfNot} ${FileExists} "$ZipDir\$0"
		DetailPrint "$0: $(TXT_ZIPMISSING)"
		Return
	${EndIf}
	DetailPrint "$0 ..."
	nsExec::ExecToLog '"$PLUGINSDIR\bdgn_unzip.exe" "$ZipDir\$0" "$INSTDIR"'
	Pop $3
	${If} $3 != 0
		StrCpy $TexFailed 1
		MessageBox MB_OK|MB_ICONEXCLAMATION "$(TXT_UNZIPFAIL)$\r$\n$0" /SD IDOK
		Return
	${EndIf}
	; a pack in parts (AI 4K: <name>_part1.zip, _part2.zip ... each under 2 GB): the other parts too
	StrCpy $8 $0 "" -10
	${If} $8 == "_part1.zip"
		StrCpy $9 $0 -5
		StrCpy $6 2
		${DoWhile} ${FileExists} "$ZipDir\$9$6.zip"
			DetailPrint "$9$6.zip ..."
			nsExec::ExecToLog '"$PLUGINSDIR\bdgn_unzip.exe" "$ZipDir\$9$6.zip" "$INSTDIR"'
			Pop $3
			${If} $3 != 0
				StrCpy $TexFailed 1
				MessageBox MB_OK|MB_ICONEXCLAMATION "$(TXT_UNZIPFAIL)$\r$\n$9$6.zip" /SD IDOK
				Return
			${EndIf}
			IntOp $6 $6 + 1
		${Loop}
	${EndIf}
	${If} $2 == ""
		Return
	${EndIf}
	FindFirst $4 $5 "$INSTDIR\baseq2\textures\$1\$2*.pak"
	${DoWhile} $5 != ""
		; the merged paks of v3.60c are <prefix>all.pak and <prefix>part<N>.pak
		StrLen $6 $2
		StrCpy $7 $5 4 $6
		StrCpy $8 $5 3 $6
		${If} $7 != "part"
		${AndIf} $8 != "all"
			CreateDirectory "$INSTDIR\baseq2\textures\_old_split_paks"
			CreateDirectory "$INSTDIR\baseq2\textures\_old_split_paks\$1"
			Rename "$INSTDIR\baseq2\textures\$1\$5" "$INSTDIR\baseq2\textures\_old_split_paks\$1\$5"
			DetailPrint "old pak moved: $1\$5 -> textures\_old_split_paks\$1"
		${EndIf}
		FindNext $4 $5
	${Loop}
	FindClose $4
FunctionEnd

; a texture pack whose zip is not in $ZipDir: shown with "(zip not found)", greyed and not selectable; else selectable,
; with the zip's size. Run again whenever $ZipDir changes (components page PRE).
!macro TEXSTATE SEC ZIP NAME MASK
	${If} $ZipDir != ""
	${AndIf} ${FileExists} "$ZipDir\${ZIP}"
		${GetSize} "$ZipDir" "/M=${MASK} /S=0K /G=0" $0 $1 $2
		SectionSetSize ${SEC} $0
		SectionSetText ${SEC} "${NAME}"
		!insertmacro ClearSectionFlag ${SEC} ${SF_RO}
	${Else}
		SectionSetText ${SEC} "${NAME}$(TXT_ZIPMISSING)"
		!insertmacro UnselectSection ${SEC}
		!insertmacro SetSectionFlag ${SEC} ${SF_RO}
	${EndIf}
!macroend
Function TexStates
	!insertmacro TEXSTATE ${SEC_HD}   "Q2TTM_Textures_HD_Standard.zip"            "HD standard" "Q2TTM_Textures_HD_Standard.zip"
	!insertmacro TEXSTATE ${SEC_HDX}  "Q2TTM_Textures_HD_Enhanced.zip"            "HD enhanced" "Q2TTM_Textures_HD_Enhanced.zip"
	!insertmacro TEXSTATE ${SEC_HDX2} "Q2TTM_Textures_HD_Enhanced_Less_Grain.zip" "HD enhanced, less grain" "Q2TTM_Textures_HD_Enhanced_Less_Grain.zip"
	!insertmacro TEXSTATE ${SEC_HDSD} "Q2TTM_Textures_AI_Stable_Diffusion.zip"    "AI (Stable Diffusion)" "Q2TTM_Textures_AI_Stable_Diffusion.zip"
	!insertmacro TEXSTATE ${SEC_HD4K} "Q2TTM_Textures_AI_4K_part1.zip"                  "AI 4K" "Q2TTM_Textures_AI_4K_part*.zip"
	!insertmacro TEXSTATE ${SEC_FX4K} "Q2TTM_Textures_HD_Effects.zip"             "HD effects" "Q2TTM_Textures_HD_Effects.zip"
	!insertmacro TEXSTATE ${SEC_SND}  "Q2TTM_Sounds_HD_44kHz.zip"                 "HD sounds 44 kHz" "Q2TTM_Sounds_HD_44kHz.zip"
FunctionEnd

; how many of the 7 zip files are in folder $R9 -> $R8
!macro TEXCOUNT1 ZIP
	${If} ${FileExists} "$R9\${ZIP}"
		IntOp $R8 $R8 + 1
	${EndIf}
!macroend
Function CountZips
	StrCpy $R8 0
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_HD_Standard.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_HD_Enhanced.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_HD_Enhanced_Less_Grain.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_AI_Stable_Diffusion.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_AI_4K_part1.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Textures_HD_Effects.zip"
	!insertmacro TEXCOUNT1 "Q2TTM_Sounds_HD_44kHz.zip"
FunctionEnd

; the texture packs: next to setup, in a Q2TTM_zip folder next to it, one level up, in the game folder, on the desktop
!macro TRYZIPDIR d
	StrCpy $R9 "${d}"
	Call CountZips
	${If} $R8 > 0
		StrCpy $ZipDir "${d}"
		StrCpy $TexCount $R8
		Return
	${EndIf}
!macroend
Function FindZips
	StrCpy $ZipDir ""
	StrCpy $TexCount 0
	!insertmacro TRYZIPDIR "$EXEDIR"
	!insertmacro TRYZIPDIR "$EXEDIR\Q2TTM_zip"
	!insertmacro TRYZIPDIR "$EXEDIR\.."
	!insertmacro TRYZIPDIR "$EXEDIR\..\Q2TTM_zip"
	!insertmacro TRYZIPDIR "$INSTDIR\Q2TTM_zip"
	!insertmacro TRYZIPDIR "$INSTDIR"
	!insertmacro TRYZIPDIR "$DESKTOP\Q2TTM_zip"
	!insertmacro TRYZIPDIR "$DESKTOP\Quake 2\Q2TTM_zip"
FunctionEnd

; page: where the texture packs are, with a Browse button
Function TexPage
	${If} $ZipDir == ""
		Call FindZips
	${EndIf}
	!insertmacro MUI_HEADER_TEXT "$(TXT_TEX_TITLE)" "$(TXT_TEX_SUB)"
	nsDialogs::Create 1018
	Pop $0
	${If} $0 == error
		Abort
	${EndIf}
	${NSD_CreateLabel} 0 0 100% 36u ""
	Pop $TexLabel
	${NSD_CreateText} 0 40u 75% 12u "$ZipDir"
	Pop $TexPath
	SendMessage $TexPath ${EM_SETREADONLY} 1 0
	${NSD_CreateButton} 77% 39u 23% 14u "$(TXT_TEX_BROWSE)"
	Pop $0
	${NSD_OnClick} $0 TexBrowse
	Call TexShow
	nsDialogs::Show
FunctionEnd
Function TexShow
	${If} $ZipDir == ""
		${NSD_SetText} $TexLabel "$(TXT_TEX_NONE)"
	${Else}
		${NSD_SetText} $TexLabel "$(TXT_TEX_FOUND)"
	${EndIf}
	${NSD_SetText} $TexPath "$ZipDir"
FunctionEnd
Function TexBrowse
	Pop $0
	${If} $ZipDir != ""
		StrCpy $1 $ZipDir
	${Else}
		StrCpy $1 $EXEDIR
	${EndIf}
	nsDialogs::SelectFolderDialog "$(TXT_TEX_PICK)" "$1"
	Pop $1
	${If} $1 == error
	${OrIf} $1 == ""
		Return
	${EndIf}
	StrCpy $R9 $1
	Call CountZips
	${If} $R8 > 0
		StrCpy $ZipDir $1
		StrCpy $TexCount $R8
		Call TexShow
	${Else}
		MessageBox MB_OK|MB_ICONEXCLAMATION "$(TXT_TEX_NOZIP)"
	${EndIf}
FunctionEnd
Function TexPageLeave
FunctionEnd

Function .onInit
	InitPluginsDir
	File "/oname=$PLUGINSDIR\bdgn_unzip.exe" "tools\bdgn_unzip.exe"
	File "/oname=$PLUGINSDIR\splash.bmp" "gfx\splash.bmp"
	File "/oname=$PLUGINSDIR\about.bmp" "gfx\about.bmp"
	File "/oname=$PLUGINSDIR\finish.bmp" "gfx\finish.bmp"
	File "/oname=$PLUGINSDIR\offer_bask.bmp" "gfx\offer_bask.bmp"
	File "/oname=$PLUGINSDIR\offer_yahoot.bmp" "gfx\offer_yahoot.bmp"
	advsplash::show 2200 600 400 -1 "$PLUGINSDIR\splash"
	Pop $0
	StrCpy $TexFailed 0
	Call FindQuake2
	Call PatchChoice
!ifdef RELSSEDIR
	; SSE: PF_XMMI_INSTRUCTIONS_AVAILABLE (6). Windows 95 has no IsProcessorFeaturePresent: the call gives "error" = no SSE
	System::Call 'kernel32::IsProcessorFeaturePresent(i 6) i .r0'
	${If} $0 == 1
		!insertmacro SelectSection ${SEC_SSE}
	${EndIf}
!endif
	Call FindZips
	Call TexStates
FunctionEnd

; Quake II: an earlier #Quake2ToTheMAX install, Steam, GOG, an uninstall entry (CD / other installers), then the usual
; folders. A folder counts when it has baseq2\pak0.pak.
!macro TRYDIR d
	${If} ${FileExists} "${d}\baseq2\pak0.pak"
		StrCpy $INSTDIR "${d}"
		Return
	${EndIf}
!macroend
Function FindQuake2
	ReadRegStr $0 HKLM "Software\Brain Drain\#Quake2ToTheMAX" "Path"
	${If} $0 != ""
		!insertmacro TRYDIR "$0"
	${EndIf}
	ReadRegStr $0 HKLM "Software\Valve\Steam" "InstallPath"
	${If} $0 != ""
		!insertmacro TRYDIR "$0\steamapps\common\Quake 2"
	${EndIf}
	ReadRegStr $0 HKCU "Software\Valve\Steam" "SteamPath"
	${If} $0 != ""
		!insertmacro TRYDIR "$0\steamapps\common\Quake 2"
	${EndIf}
	StrCpy $1 0
	${Do}
		EnumRegKey $2 HKLM "Software\GOG.com\Games" $1
		${If} $2 == ""
			${Break}
		${EndIf}
		ReadRegStr $0 HKLM "Software\GOG.com\Games\$2" "path"
		${If} $0 != ""
			!insertmacro TRYDIR "$0"
		${EndIf}
		IntOp $1 $1 + 1
	${Loop}
	StrCpy $1 0
	${Do}
		EnumRegKey $2 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall" $1
		${If} $2 == ""
			${Break}
		${EndIf}
		ReadRegStr $3 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$2" "DisplayName"
		StrCpy $4 $3 7
		${If} $4 == "Quake I"
		${OrIf} $4 == "Quake 2"
			ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\$2" "InstallLocation"
			${If} $0 != ""
				!insertmacro TRYDIR "$0"
			${EndIf}
		${EndIf}
		IntOp $1 $1 + 1
	${Loop}
	!insertmacro TRYDIR "C:\Quake2"
	!insertmacro TRYDIR "C:\Quake II"
	!insertmacro TRYDIR "C:\Quake 2"
	!insertmacro TRYDIR "C:\Games\Quake2"
	!insertmacro TRYDIR "C:\Games\Quake II"
	!insertmacro TRYDIR "C:\Games\Quake 2"
	!insertmacro TRYDIR "$PROGRAMFILES\Quake II"
	!insertmacro TRYDIR "$PROGRAMFILES\Quake 2"
	!insertmacro TRYDIR "$PROGRAMFILES\Steam\steamapps\common\Quake 2"
	!insertmacro TRYDIR "$DESKTOP\Quake 2"
	!insertmacro TRYDIR "$DESKTOP\Quake2"
	!insertmacro TRYDIR "D:\Quake2"
	!insertmacro TRYDIR "D:\Games\Quake2"
FunctionEnd

; the 3.20 patch is chosen when pak1.pak or pak2.pak is missing (selectable either way)
Function PatchChoice
	${If} ${FileExists} "$INSTDIR\baseq2\pak1.pak"
	${AndIf} ${FileExists} "$INSTDIR\baseq2\pak2.pak"
		!insertmacro UnselectSection ${SEC_PATCH}
	${Else}
		!insertmacro SelectSection ${SEC_PATCH}
	${EndIf}
FunctionEnd

Function DirLeave
	${IfNot} ${FileExists} "$INSTDIR\baseq2\pak0.pak"
		MessageBox MB_YESNO|MB_ICONQUESTION "$(TXT_NOQ2)" /SD IDYES IDYES +2
		Abort
	${EndIf}
FunctionEnd

Function ComponentsPre
	Call PatchChoice
	Call TexStates
FunctionEnd

Var AboutBmp
Var AboutBmpHandle
Function AboutPage
	!insertmacro MUI_HEADER_TEXT "$(TXT_ABOUT_SUB)" "$(TXT_ABOUT_TITLE) ${VERSION}"
	nsDialogs::Create 1018
	Pop $0
	${If} $0 == error
		Abort
	${EndIf}
	${NSD_CreateBitmap} 0 0 100% 62% ""
	Pop $AboutBmp
	${NSD_SetStretchedImage} $AboutBmp "$PLUGINSDIR\about.bmp" $AboutBmpHandle
	${NSD_CreateLabel} 0 66% 100% 34% "$(TXT_ABOUT_TEXT)"
	Pop $0
	nsDialogs::Show
	${NSD_FreeImage} $AboutBmpHandle
FunctionEnd

; the finish page shows a picture from the game instead of the welcome picture
Var FinishBmpHandle
Function FinishShow
	${NSD_SetImage} $mui.FinishPage.Image "$PLUGINSDIR\finish.bmp" $FinishBmpHandle
FunctionEnd

Function RunGame
	SetOutPath "$INSTDIR"
	Exec '"$INSTDIR\quake2.exe"'
FunctionEnd
