; OpenXR SimHub Alerts installer.
; makensis -DVERSION=<x.y.z> -DSOURCE=<package dir> [-DOUTFILE=<exe>] OpenXRSimHubAlerts.nsi
; SOURCE is the release zip layout: layer\, plugin\, README.md, LICENSE.

Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include FileFunc.nsh
!include layers.nsh

!ifndef VERSION
  !define VERSION 0.0.0
!endif
!ifndef SOURCE
  !error "Pass -DSOURCE=<package dir>"
!endif
!ifndef OUTFILE
  !define OUTFILE "OpenXRSimHubAlerts-Setup-v${VERSION}.exe"
!endif

!define APP        "OpenXR SimHub Alerts"
!define PUBLISHER  "David Long"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenXRSimHubAlerts"
!define LAYERS_KEY "Software\Khronos\OpenXR\1\ApiLayers\Implicit"
!define MANIFEST   "OpenXRSimHubAlerts.json"
!define PLUGIN     "OpenXRSimHubAlerts.Plugin.dll"
!define SIMHUB_EXE "SimHubWPF.exe"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\OpenXRSimHubAlerts"
RequestExecutionLevel admin

VIProductVersion "${VERSION}.0"
VIAddVersionKey ProductName "${APP}"
VIAddVersionKey ProductVersion "${VERSION}"
VIAddVersionKey FileVersion "${VERSION}"
VIAddVersionKey FileDescription "${APP} Setup"
VIAddVersionKey LegalCopyright "Copyright (c) 2026 ${PUBLISHER}"

Var SimHubDir
Var OldSimHubDir  ; SimHub folder used by the previous install, if any

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\LICENSE"

!define MUI_PAGE_CUSTOMFUNCTION_PRE SimHubPagePre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE SimHubPageLeave
!define MUI_DIRECTORYPAGE_VARIABLE $SimHubDir
!define MUI_PAGE_HEADER_TEXT "SimHub folder"
!define MUI_PAGE_HEADER_SUBTEXT "Choose where SimHub is installed."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Setup could not find SimHub. Select the folder that contains ${SIMHUB_EXE}; the plugin installs there."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "SimHub folder"
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "${APP} is installed.$\r$\n$\r$\nStart SimHub and enable ${APP} when it asks."
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE English

; Blocks until SimHub is closed: it locks the plugin DLL. Cancel aborts.
!macro DefineWaitForSimHubClosed UN
  Function ${UN}WaitForSimHubClosed
    retry:
      nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq ${SIMHUB_EXE}" /NH | find /I "${SIMHUB_EXE}"'
      Pop $0  ; find exits 0 when SimHub is running
      Pop $1
      ${If} $0 == 0
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Close SimHub, then click Retry." /SD IDCANCEL IDRETRY retry
        Abort
      ${EndIf}
  FunctionEnd
!macroend
!insertmacro DefineWaitForSimHubClosed ""
!insertmacro DefineWaitForSimHubClosed "un."

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APP} needs 64-bit Windows." /SD IDOK
    Abort
  ${EndIf}
  SetRegView 64

  ReadRegStr $OldSimHubDir HKLM "${UNINST_KEY}" "SimHubDir"
  StrCpy $SimHubDir $OldSimHubDir
  ${IfNot} ${FileExists} "$SimHubDir\${SIMHUB_EXE}"
    ReadRegStr $SimHubDir HKCU "Software\SimHub" "InstallDirectory"
  ${EndIf}

  ${IfNot} ${FileExists} "$SimHubDir\${SIMHUB_EXE}"
    ${If} ${Silent}
      SetErrorLevel 2
      Abort
    ${EndIf}
    StrCpy $SimHubDir "$PROGRAMFILES32\SimHub"
  ${EndIf}
FunctionEnd

Function SimHubPagePre
  ${If} ${FileExists} "$SimHubDir\${SIMHUB_EXE}"
    Abort  ; SimHub found: skip the page
  ${EndIf}
FunctionEnd

Function SimHubPageLeave
  ${IfNot} ${FileExists} "$SimHubDir\${SIMHUB_EXE}"
    MessageBox MB_ICONEXCLAMATION "That folder does not contain ${SIMHUB_EXE}."
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  Call WaitForSimHubClosed

  SetOutPath "$INSTDIR"
  File "${SOURCE}\layer\OpenXRSimHubAlerts.dll"
  File "${SOURCE}\layer\${MANIFEST}"
  File "${SOURCE}\README.md"
  File "${SOURCE}\LICENSE"

  SetOutPath "$SimHubDir"
  File "${SOURCE}\plugin\${PLUGIN}"
  ${If} $OldSimHubDir != ""
  ${AndIf} $OldSimHubDir != $SimHubDir
    Delete "$OldSimHubDir\${PLUGIN}"
  ${EndIf}

  !insertmacro PurgeLayers HKLM "${LAYERS_KEY}" "${MANIFEST}"
  !insertmacro PurgeLayers HKCU "${LAYERS_KEY}" "${MANIFEST}"
  WriteRegDWORD HKLM "${LAYERS_KEY}" "$INSTDIR\${MANIFEST}" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "https://github.com/Teqqles/OpenXRSimhubAlerts"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "SimHubDir" "$SimHubDir"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Function un.onInit
  SetRegView 64
  ReadRegStr $SimHubDir HKLM "${UNINST_KEY}" "SimHubDir"
FunctionEnd

Section "Uninstall"
  Call un.WaitForSimHubClosed

  ${If} $SimHubDir != ""
    Delete "$SimHubDir\${PLUGIN}"
  ${EndIf}
  DeleteRegValue HKLM "${LAYERS_KEY}" "$INSTDIR\${MANIFEST}"

  Delete /REBOOTOK "$INSTDIR\OpenXRSimHubAlerts.dll"
  Delete "$INSTDIR\${MANIFEST}"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /REBOOTOK "$INSTDIR"

  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
