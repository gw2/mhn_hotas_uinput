; Installer for the Flightstick 2 Windows driver (NSIS 3). Built by build-installer.sh.
;
; Installs the app, and (as chosen on the components page) the official vJoy
; installer, the vJoy device setup and the stick's WinUSB driver, all under the
; one admin prompt the installer itself needs.

Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef VERSION
	!define VERSION "0.0.0"
!endif
!define ROOT        ".."        ; makensis runs from this script's folder
!define VJOY_SETUP  "vJoySetup_v2.2.2.0_Win10_Win11.exe"
!define VJOY_DIR    "$PROGRAMFILES64\vJoy"
!define UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\Flightstick2"
!define APP_KEY     "Software\Flightstick2"
!define RUN_KEY     "Software\Microsoft\Windows\CurrentVersion\Run"
!define TRAY_EXIT   604         ; IDM_TRAY_EXIT in hori_gui.c

Name "Flightstick 2 driver ${VERSION}"
OutFile "${ROOT}/Flightstick2-Setup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\Flightstick 2"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "Flightstick 2 driver ${VERSION}"

!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open Flightstick 2 to check everything works"
!define MUI_FINISHPAGE_RUN_FUNCTION RunApp
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README-Windows.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the readme"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; Close a running copy so its files can be replaced: ask the tray app to exit,
; then stop anything still running.
!macro CloseRunning
	FindWindow $0 "HoriTest"
	${If} $0 <> 0
		DetailPrint "Closing the running Flightstick 2..."
		SendMessage $0 ${WM_COMMAND} ${TRAY_EXIT} 0 /TIMEOUT=3000
		Sleep 1000
	${EndIf}
	nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM hori-test.exe'
	Pop $0
	nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM hori.exe'
	Pop $0
!macroend

; Start the app as the signed-in user rather than elevated like the installer.
Function RunApp
	Exec '"$WINDIR\explorer.exe" "$INSTDIR\hori-test.exe"'
FunctionEnd

Function .onInit
	${IfNot} ${RunningX64}
		MessageBox MB_ICONSTOP "This driver needs 64-bit Windows 10 or 11."
		Abort
	${EndIf}
	${IfNot} ${AtLeastWin10}
		MessageBox MB_ICONSTOP "This driver needs Windows 10 or 11."
		Abort
	${EndIf}
	SetRegView 64
	SetShellVarContext all
	ReadRegStr $0 HKLM "${APP_KEY}" "InstallDir"
	${If} $0 != ""
		StrCpy $INSTDIR $0
	${EndIf}
FunctionEnd

Section "Flightstick 2 driver (required)" SecApp
	SectionIn RO
	!insertmacro CloseRunning
	SetOutPath "$INSTDIR"
	File "${ROOT}/hori.exe"
	File "${ROOT}/hori-test.exe"
	File "${ROOT}/hori-driver.exe"
	File "${ROOT}/README-Windows.txt"
	File "${ROOT}/THIRD-PARTY-NOTICES.txt"
	File "${ROOT}/LICENSE"
	File "${ROOT}/LICENSE-libusb.txt"
	SetOutPath "$INSTDIR\licenses"
	File "/oname=libwdi-COPYING.txt" "${ROOT}/third_party/libwdi/COPYING"
	File "/oname=libwdi-COPYING-LGPL.txt" "${ROOT}/third_party/libwdi/COPYING-LGPL"
	File "/oname=vJoy-LICENSE.txt" "${ROOT}/third_party/vjoy/LICENSE.txt"
	SetOutPath "$INSTDIR"

	CreateDirectory "$SMPROGRAMS\Flightstick 2"
	CreateShortcut "$SMPROGRAMS\Flightstick 2\Flightstick 2 driver.lnk" "$INSTDIR\hori.exe"
	Delete "$SMPROGRAMS\Flightstick 2\Flightstick 2 test and setup.lnk"   ; 0.3.0 pre-release name
	CreateShortcut "$SMPROGRAMS\Flightstick 2\Flightstick 2.lnk" "$INSTDIR\hori-test.exe"
	CreateShortcut "$SMPROGRAMS\Flightstick 2\Uninstall Flightstick 2 driver.lnk" "$INSTDIR\uninstall.exe"

	WriteUninstaller "$INSTDIR\uninstall.exe"
	WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
	WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "Flightstick 2 driver"
	WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
	WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "gw2"
	WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "https://github.com/gw2/mhn_hotas_uinput"
	WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\hori-test.exe"
	WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
	WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
	WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "vJoy (the virtual joystick games see)" SecVJoy
	; Both files: uninstalling vJoy while the DLL is in use leaves just the DLL.
	${If} ${FileExists} "${VJOY_DIR}\x64\vJoyInterface.dll"
	${AndIf} ${FileExists} "${VJOY_DIR}\x64\vJoyConfig.exe"
		DetailPrint "vJoy is already installed."
	${Else}
		InitPluginsDir
		SetOutPath "$PLUGINSDIR"
		File "${ROOT}/installer/build/${VJOY_SETUP}"
		DetailPrint "Installing vJoy 2.2.2.0 (this can take a minute)..."
		ExecWait '"$PLUGINSDIR\${VJOY_SETUP}" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART' $0
		DetailPrint "vJoy installer finished (code $0)."
		SetOutPath "$INSTDIR"
	${EndIf}
SectionEnd

Section "Set up vJoy device 1 for the stick" SecVJoyCfg
	${If} ${FileExists} "${VJOY_DIR}\x64\vJoyConfig.exe"
		DetailPrint "Setting vJoy device 1 to 8 axes, 32 buttons, 3 POV hats..."
		nsExec::ExecToLog '"${VJOY_DIR}\x64\vJoyConfig.exe" 1 -f -a x y z rx ry rz sl0 sl1 -b 32 -p 3'
		Pop $0
		DetailPrint "vJoyConfig finished (code $0)."
	${Else}
		DetailPrint "vJoy isn't installed, so its device wasn't set up."
	${EndIf}
SectionEnd

Section "The stick's USB driver (WinUSB)" SecWinUSB
	DetailPrint "Installing the stick's WinUSB driver..."
	nsExec::ExecToLog '"$INSTDIR\hori-driver.exe" install'
	Pop $0
	${If} $0 != 0
		MessageBox MB_ICONEXCLAMATION "Installing the stick's WinUSB driver failed (code $0). The details are in the list below; hori-test.exe's Setup check can try again."
	${EndIf}
SectionEnd

Section /o "Start with Windows (in the tray)" SecStartup
	; The same entry the tray menu's "Start with Windows" sets.
	WriteRegStr HKCU "${RUN_KEY}" "Flightstick2" '"$INSTDIR\hori-test.exe" -T'
	Delete "$SMSTARTUP\Flightstick 2 driver.lnk"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "hori-test.exe (the tray app: driver, test window, setup check and wizard), hori.exe (the same driver in a console window), hori-driver.exe, and Start-menu shortcuts."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecVJoy} "Installs vJoy 2.2.2.0 (MIT licensed, by Shaul Eizikovich / Brunner Elektronik AG) if it isn't already installed. It has its own uninstaller."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecVJoyCfg} "Sets vJoy device 1 to 8 axes, 32 buttons and 3 POV hats. This replaces device 1's current setup."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecWinUSB} "Points the stick (USB 06D3:0F10) at Windows' built-in WinUSB driver. Like Zadig, it trusts a certificate made only for this driver package."
	!insertmacro MUI_DESCRIPTION_TEXT ${SecStartup} "Starts Flightstick 2 in the system tray when you sign in, so the stick works in games right away. You can turn this off from the tray icon's menu."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function un.onInit
	SetRegView 64
	SetShellVarContext all
FunctionEnd

Section "Uninstall"
	!insertmacro CloseRunning
	MessageBox MB_YESNO|MB_ICONQUESTION "Also remove the stick's WinUSB driver?$\n$\nChoose No if another program uses the stick through WinUSB." IDNO keep_driver
		nsExec::ExecToLog '"$INSTDIR\hori-driver.exe" uninstall'
		Pop $0
	keep_driver:

	DeleteRegValue HKCU "${RUN_KEY}" "Flightstick2"
	Delete "$SMSTARTUP\Flightstick 2 driver.lnk"
	Delete "$SMPROGRAMS\Flightstick 2\Flightstick 2.lnk"
	Delete "$SMPROGRAMS\Flightstick 2\Flightstick 2 test and setup.lnk"
	Delete "$SMPROGRAMS\Flightstick 2\Flightstick 2 driver.lnk"
	Delete "$SMPROGRAMS\Flightstick 2\Flightstick 2 test and setup.lnk"
	Delete "$SMPROGRAMS\Flightstick 2\Uninstall Flightstick 2 driver.lnk"
	RMDir "$SMPROGRAMS\Flightstick 2"

	Delete "$INSTDIR\hori.exe"
	Delete "$INSTDIR\hori-test.exe"
	Delete "$INSTDIR\hori-driver.exe"
	Delete "$INSTDIR\README-Windows.txt"
	Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
	Delete "$INSTDIR\LICENSE"
	Delete "$INSTDIR\LICENSE-libusb.txt"
	Delete "$INSTDIR\licenses\libwdi-COPYING.txt"
	Delete "$INSTDIR\licenses\libwdi-COPYING-LGPL.txt"
	Delete "$INSTDIR\licenses\vJoy-LICENSE.txt"
	RMDir "$INSTDIR\licenses"
	Delete "$INSTDIR\uninstall.exe"
	RMDir "$INSTDIR"

	DeleteRegKey HKLM "${UNINST_KEY}"
	DeleteRegValue HKLM "${APP_KEY}" "InstallDir"
	DeleteRegKey /ifempty HKLM "${APP_KEY}"

	DetailPrint "vJoy was left installed; remove it from Settings > Apps if nothing else uses it."
	DetailPrint "Your settings and logs are in %APPDATA%\Flightstick2 and %LOCALAPPDATA%\Flightstick2."
SectionEnd
