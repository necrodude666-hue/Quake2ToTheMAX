; #Quake2ToTheMAX installer - the "special offers" joke (owner 2026-11-23): two FAKE bundleware offers in the style of
; the 2000s installers, the "BASK Toolbar" and the "Yahoot! Download Helper". Everything here is fake: nothing is
; downloaded, installed or changed, the check boxes do nothing. Accept shows a fake download (30 GB at a few KB/s,
; weeks left) with a Cancel button that goes back to the installation; Decline (the Next button) goes on.
; Included by Q2TTM.nsi; the pages are listed there after the texture pack page.

Var OfferId			; 1 BASK, 2 Yahoot
Var OfferAccepted
Var OfferImg
Var OfferImgH
Var DLLabel
Var DLFile
Var DLBar
Var DLDone			; KB "downloaded"
Var DLTicks

!define DL_TOTAL_KB 31457280	; 30 GB
!define DL_SPEEDS "50231941275021183320"	; tenths of KB/s, two digits each: 5.0 2.3 1.9 4.1 2.7 5.0 2.1 1.8 3.3 2.0

Function OfferCommon
	; $R0 picture, $R1 title, $R2 subtitle, $R3 text, $R4 box 1, $R5 box 2, $R6 box 3, $R7 small print
	StrCpy $OfferAccepted 0
	!insertmacro MUI_HEADER_TEXT "$R1" "$R2"
	nsDialogs::Create 1018
	Pop $0
	${If} $0 == error
		Abort
	${EndIf}
	${NSD_CreateBitmap} 0 0 48 48 ""
	Pop $OfferImg
	${NSD_SetImage} $OfferImg "$PLUGINSDIR\$R0" $OfferImgH
	${NSD_CreateLabel} 40u 0 86% 44u "$R3"
	Pop $0
	${NSD_CreateCheckbox} 40u 46u 86% 10u "$R4"
	Pop $0
	${NSD_Check} $0
	${NSD_CreateCheckbox} 40u 57u 86% 10u "$R5"
	Pop $0
	${NSD_Check} $0
	${NSD_CreateCheckbox} 40u 68u 86% 10u "$R6"
	Pop $0
	${NSD_Check} $0
	${NSD_CreateButton} 40u 84u 120u 20u "Accept (Recommended)"
	Pop $0
	${NSD_OnClick} $0 OfferAccept
	${NSD_CreateLabel} 40u 108u 86% 32u "$R7"
	Pop $0
	; the Next button is the Decline button on these pages (NSIS gives it its own text again on the next page)
	GetDlgItem $0 $HWNDPARENT 1
	SendMessage $0 ${WM_SETTEXT} 0 "STR:Decline"
	nsDialogs::Show
	${NSD_FreeImage} $OfferImgH
FunctionEnd

Function OfferBaskPage
	StrCpy $OfferId 1
	StrCpy $R0 "offer_bask.bmp"
	StrCpy $R1 "Special offer: the BASK Toolbar"
	StrCpy $R2 "Free! Recommended by 9 out of 10 toolbars."
	StrCpy $R3 "Get the BASK Toolbar - the smartest way to search the web! Search from any window, see the weather, your horoscope and the latest celebrity news, and get free smileys for your e-mail. BASK Toolbar makes #Quake2ToTheMAX up to 0 % faster."
	StrCpy $R4 "Make BASK Search my home page"
	StrCpy $R5 "Make BASK Search my default search engine"
	StrCpy $R6 "Install the BASK Smiley Pack (1,200 free smileys!)"
	StrCpy $R7 "By clicking Accept you agree to the BASK End User License Agreement (847 pages), the BASK Privacy Policy and the BASK Policy About Privacy Policies. BASK Toolbar can be removed at any time with the BASK Toolbar Removal Toolbar."
	Call OfferCommon
FunctionEnd

Function OfferYahootPage
	StrCpy $OfferId 2
	StrCpy $R0 "offer_yahoot.bmp"
	StrCpy $R1 "Special offer: Yahoot! Download Helper"
	StrCpy $R2 "Download up to 3x faster!*"
	StrCpy $R3 "Yahoot! Download Helper makes every download faster, safer and more Yahoot!. It also keeps you up to date with Yahoot! News, Yahoot! Weather and your Yahoot! Mail - right in your system tray, every time you start your computer."
	StrCpy $R4 "Also install Yahoot! Messenger with Voice"
	StrCpy $R5 "Also install Yahoot! Weather Buddy"
	StrCpy $R6 "Start Yahoot! Download Helper with Windows"
	StrCpy $R7 "*Compared to downloading with a 14.4k modem during a thunderstorm. Results not typical. By clicking Accept you agree to the Yahoot! Terms of Service and to receive Yahoot! Special Offers by e-mail, post and fax."
	Call OfferCommon
FunctionEnd

Function OfferAccept
	Pop $0
	StrCpy $OfferAccepted 1
	SendMessage $HWNDPARENT 0x408 1 0	; to the next page: the "download"
FunctionEnd

Function OfferLeave
FunctionEnd

; ---------------------------------------------------------------- the fake download
Function FakeDLPage
	${If} $OfferAccepted != 1
		Abort	; declined: no download page
	${EndIf}
	${If} $OfferId == 1
		!insertmacro MUI_HEADER_TEXT "Downloading BASK Toolbar..." "Please wait while BASK Toolbar is downloaded. Do not turn off your computer."
		StrCpy $R0 "bask_toolbar_core_part0001.cab"
	${Else}
		!insertmacro MUI_HEADER_TEXT "Downloading Yahoot! Download Helper..." "Yahoot! Download Helper is helping itself download. Do not turn off your computer."
		StrCpy $R0 "yahoot_helper_full_part0001.cab"
	${EndIf}
	nsDialogs::Create 1018
	Pop $0
	${If} $0 == error
		Abort
	${EndIf}
	${NSD_CreateLabel} 0 0 100% 10u "File 1 of 4,812: $R0"
	Pop $DLFile
	${NSD_CreateProgressBar} 0 14u 100% 12u ""
	Pop $DLBar
	SendMessage $DLBar ${PBM_SETRANGE32} 0 10000
	SendMessage $DLBar ${PBM_SETPOS} 0 0
	${NSD_CreateLabel} 0 32u 100% 40u ""
	Pop $DLLabel
	${NSD_CreateButton} 0 80u 80u 16u "Cancel download"
	Pop $0
	${NSD_OnClick} $0 FakeDLCancel
	StrCpy $DLDone 0
	StrCpy $DLTicks 0
	Call FakeDLTick
	GetDlgItem $0 $HWNDPARENT 1	; Next and Back are off while it "downloads"
	EnableWindow $0 0
	GetDlgItem $0 $HWNDPARENT 3
	EnableWindow $0 0
	nsDialogs::CreateTimer FakeDLTick 1000
	nsDialogs::Show
FunctionEnd

Function FakeDLTick
	; speed of this second (tenths of KB/s) from the list
	IntOp $1 $DLTicks % 10
	IntOp $1 $1 * 2
	StrCpy $2 "${DL_SPEEDS}" 2 $1
	IntOp $2 $2 + 0
	IntOp $DLTicks $DLTicks + 1
	; downloaded: KB (rounded down) of this second
	IntOp $3 $2 / 10
	IntOp $DLDone $DLDone + $3
	; time left in seconds at this speed: remaining KB * 10 / tenths of KB/s
	IntOp $4 ${DL_TOTAL_KB} - $DLDone
	IntOp $4 $4 * 10
	IntOp $4 $4 / $2
	IntOp $5 $4 / 604800	; weeks
	IntOp $6 $4 % 604800
	IntOp $6 $6 / 86400	; days
	IntOp $7 $4 % 86400
	IntOp $7 $7 / 3600	; hours
	IntOp $8 $4 % 3600
	IntOp $8 $8 / 60	; minutes
	; percent with two decimals
	IntOp $9 $DLDone * 10000
	IntOp $9 $9 / ${DL_TOTAL_KB}
	SendMessage $DLBar ${PBM_SETPOS} $9 0
	IntOp $R1 $9 / 100
	IntOp $R2 $9 % 100
	${If} $R2 < 10
		StrCpy $R2 "0$R2"
	${EndIf}
	IntOp $R3 $2 / 10
	IntOp $R4 $2 % 10
	${NSD_SetText} $DLLabel "Downloaded: $DLDone KB of 30.0 GB ($R1.$R2 %)$\r$\nTransfer rate: $R3.$R4 KB/s$\r$\nTime left: $5 weeks, $6 days, $7 hours, $8 minutes$\r$\n$\r$\nTip: for a faster download, close all other programs and stop touching the mouse."
FunctionEnd

Function FakeDLCancel
	Pop $0
	nsDialogs::KillTimer FakeDLTick
	MessageBox MB_OK|MB_ICONINFORMATION "Download cancelled.$\r$\n$\r$\n(Relax: it was a joke. Nothing was downloaded or installed - just like in 1999, but faster.)"
	StrCpy $OfferAccepted 0
	GetDlgItem $0 $HWNDPARENT 1
	EnableWindow $0 1
	GetDlgItem $0 $HWNDPARENT 3
	EnableWindow $0 1
	SendMessage $HWNDPARENT 0x408 1 0	; back to the installation: the next page
FunctionEnd

Function FakeDLLeave
	nsDialogs::KillTimer FakeDLTick
FunctionEnd
