; Installer for psxrdc2: either a per-user install (Start menu, uninstaller, optional right-click menu)
; or a portable copy in a folder of your choice that touches nothing else. Any drive, any folder.
Unicode true
!include MUI2.nsh
!include nsDialogs.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro un.GetParameters
!insertmacro un.GetOptions

!define APPNAME "PSX PS2 Redump Checker"
!define EXE "psxrdc2.exe"
!define FOLDER "PSXRedumpChecker"
!define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\psxrdc2"

Name "${APPNAME}"
OutFile "..\release\psxrdc2-setup.exe"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${FOLDER}"
BrandingText "psxrdc2"

!define MUI_ICON "..\res\app.ico"
!define MUI_UNICON "..\res\app.ico"

Var Portable
Var rbInstalled
Var rbPortable
Var cbMenu
Var cbDesktop
Var txtDir
Var btnBrowse
Var DoMenu
Var DoDesktop
Var Elevated
Var ExistDir
Var HasExisting
Var UpdateMode
Var rbUpdate
Var rbChange

Page custom UpdatePage UpdateLeave
Page custom ModePage ModeLeave
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Command line (also for silent installs): /S  /PORTABLE  /NOMENU  /DESKTOP  /D=folder
; /ELEVATED is passed by the installer itself when it restarts as administrator.
Function .onInit
  StrCpy $Portable 0
  StrCpy $DoMenu 1
  StrCpy $DoDesktop 0
  StrCpy $Elevated 0
  ${GetParameters} $0
  ${GetOptions} $0 "/ELEVATED" $1
  ${IfNot} ${Errors}
    StrCpy $Elevated 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $0 "/PORTABLE" $1
  ${IfNot} ${Errors}
    StrCpy $Portable 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $0 "/NOMENU" $1
  ${IfNot} ${Errors}
    StrCpy $DoMenu 0
  ${EndIf}
  ClearErrors
  ${GetOptions} $0 "/DESKTOP" $1
  ${IfNot} ${Errors}
    StrCpy $DoDesktop 1
  ${EndIf}
  ClearErrors
  ${If} $Portable == 1
  ${AndIf} $INSTDIR == "$LOCALAPPDATA\Programs\${FOLDER}"
    StrCpy $INSTDIR "$EXEDIR\${FOLDER}"
  ${EndIf}
  ; An existing per-user install? Then offer to update it in place.
  StrCpy $HasExisting 0
  StrCpy $UpdateMode 0
  ReadRegStr $ExistDir HKCU "${UNKEY}" "InstallLocation"
  ${If} $ExistDir != ""
  ${AndIf} ${FileExists} "$ExistDir\${EXE}"
    StrCpy $HasExisting 1
    ; silent installs without /D= update the existing folder
    ${If} ${Silent}
    ${AndIf} $Portable == 0
    ${AndIf} $INSTDIR == "$LOCALAPPDATA\Programs\${FOLDER}"
      StrCpy $INSTDIR $ExistDir
    ${EndIf}
  ${EndIf}
FunctionEnd

; Writable? If not (Program Files, a drive root...), offer to restart as administrator with the same choices.
; Leaves 1 on the stack when the install can go ahead, else 0.
Function CheckDir
  ClearErrors
  CreateDirectory "$INSTDIR"
  FileOpen $0 "$INSTDIR\.psxrdc2_write_test" w
  ${If} ${Errors}
    MessageBox MB_YESNO|MB_ICONQUESTION "$INSTDIR needs administrator rights. Restart the installer as administrator?" IDNO stay
    StrCpy $2 "/ELEVATED"
    ${If} $Portable == 1
      StrCpy $2 "$2 /PORTABLE"
    ${EndIf}
    ${If} $DoMenu == 0
      StrCpy $2 "$2 /NOMENU"
    ${EndIf}
    ${If} $DoDesktop == 1
      StrCpy $2 "$2 /DESKTOP"
    ${EndIf}
    ExecShell "runas" "$EXEPATH" '$2 /D=$INSTDIR'
    Quit
    stay:
    Push 0
    Return
  ${EndIf}
  FileClose $0
  Delete "$INSTDIR\.psxrdc2_write_test"
  Push 1
FunctionEnd

Function UpdatePage
  ${If} $Elevated == 1
  ${OrIf} $HasExisting == 0
    Abort
  ${EndIf}
  !insertmacro MUI_HEADER_TEXT "Already installed" "${APPNAME} is already installed on this computer."
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateLabel} 0 0 100% 10u "An existing installation was found in:"
  Pop $0
  ${NSD_CreateLabel} 0 10u 100% 10u "$ExistDir"
  Pop $0
  ${NSD_CreateRadioButton} 0 28u 100% 11u "Update it  (recommended)"
  Pop $rbUpdate
  ${NSD_CreateLabel} 12u 40u 95% 22u "Replaces the program in the same folder. Your settings, Redump data, downloaded tools, shortcuts and right-click menu are kept. Close ${APPNAME} first."
  Pop $0
  ${NSD_CreateRadioButton} 0 70u 100% 11u "Choose a different location or installation type"
  Pop $rbChange
  ${NSD_Check} $rbUpdate
  nsDialogs::Show
FunctionEnd

Function UpdateLeave
  ${NSD_GetState} $rbUpdate $0
  ${If} $0 == 1
    StrCpy $UpdateMode 1
    StrCpy $Portable 0
    StrCpy $INSTDIR $ExistDir
    ReadRegStr $1 HKCU "Software\Classes\Directory\shell\PSXRedumpChecker2" ""
    ${If} $1 != ""
      StrCpy $DoMenu 1
    ${Else}
      StrCpy $DoMenu 0
    ${EndIf}
    ${If} ${FileExists} "$DESKTOP\${APPNAME}.lnk"
      StrCpy $DoDesktop 1
    ${Else}
      StrCpy $DoDesktop 0
    ${EndIf}
    Call CheckDir
    Pop $0
    ${If} $0 != 1
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Function SetDefaultDir
  ${NSD_GetState} $rbPortable $0
  ${If} $0 == 1
    ${NSD_SetText} $txtDir "$EXEDIR\${FOLDER}"
  ${Else}
    ${NSD_SetText} $txtDir "$LOCALAPPDATA\Programs\${FOLDER}"
  ${EndIf}
FunctionEnd

Function OnModeClick
  Call SetDefaultDir
FunctionEnd

; Folder picker: the modern Windows open dialog (every drive, This PC, address bar), run through PowerShell so a
; failure there can never take the installer down. Open the wanted folder and press Open.
Function OnBrowse
  ${NSD_GetText} $txtDir $4
  System::Call 'kernel32::SetEnvironmentVariableW(w "PSXDIR", w r4)'
  ; The result goes through a file: shell extensions (Google Drive, ...) write their own noise to the output.
  Delete "$TEMP\psxrdc2_pick.txt"
  System::Call 'kernel32::SetEnvironmentVariableW(w "PSXOUT", w "$TEMP\psxrdc2_pick.txt")'
  nsExec::ExecToStack 'powershell.exe -NoProfile -NonInteractive -STA -ExecutionPolicy Bypass -Command "Add-Type -AssemblyName System.Windows.Forms; $$p=$$env:PSXDIR; while($$p -and -not (Test-Path -LiteralPath $$p)){ $$p=Split-Path -Parent $$p }; $$d=New-Object System.Windows.Forms.OpenFileDialog; $$d.Title=$\'Choose the install folder: open it, then press Open$\'; $$d.ValidateNames=$$false; $$d.CheckFileExists=$$false; $$d.CheckPathExists=$$true; $$d.FileName=$\'Select this folder$\'; if($$p){ $$d.InitialDirectory=$$p }; if($$d.ShowDialog() -eq $\'OK$\'){ [IO.File]::WriteAllText($$env:PSXOUT, (Split-Path -Parent $$d.FileName)) }"'
  Pop $0
  Pop $2
  StrCpy $1 ""
  ClearErrors
  FileOpen $5 "$TEMP\psxrdc2_pick.txt" r
  ${IfNot} ${Errors}
    FileRead $5 $1
    FileClose $5
  ${EndIf}
  ClearErrors
  Delete "$TEMP\psxrdc2_pick.txt"
  ${If} $1 != ""
    ; a whole drive was picked: put the program in its own folder there
    StrLen $2 $1
    IntOp $2 $2 - 1
    StrCpy $3 $1 1 $2
    ${If} $3 == "\"
      StrCpy $1 "$1${FOLDER}"
    ${EndIf}
    ${NSD_SetText} $txtDir "$1"
  ${EndIf}
FunctionEnd

Function ModePage
  ${If} $Elevated == 1
  ${OrIf} $UpdateMode == 1
    Abort
  ${EndIf}
  !insertmacro MUI_HEADER_TEXT "Installation type" "Choose how and where to set up ${APPNAME}."
  nsDialogs::Create 1018
  Pop $0
  ${NSD_CreateRadioButton} 0 0 100% 11u "Install  (recommended)"
  Pop $rbInstalled
  ${NSD_CreateLabel} 12u 11u 95% 21u "For your user account only, no administrator rights needed: Start menu entry, an uninstaller, and your settings and Redump data kept in %LOCALAPPDATA%\psxrdc2."
  Pop $0
  ${NSD_CreateCheckbox} 12u 33u 95% 10u "Add 'Check PS1/PS2 Redump dump' to the folder right-click menu"
  Pop $cbMenu
  ${NSD_CreateCheckbox} 12u 44u 95% 10u "Create a desktop shortcut"
  Pop $cbDesktop
  ${NSD_CreateRadioButton} 0 59u 100% 11u "Portable"
  Pop $rbPortable
  ${NSD_CreateLabel} 12u 70u 95% 21u "Copies just psxrdc2.exe into the folder below. Settings, Redump data and downloaded tools stay in that same folder; nothing is added to the registry or the Start menu."
  Pop $0

  ${NSD_CreateLabel} 0 96u 100% 10u "Install to (any drive or folder; Program Files will ask for administrator rights):"
  Pop $0
  ${NSD_CreateText} 0 107u 188u 12u "$INSTDIR"
  Pop $txtDir
  ${NSD_CreateButton} 191u 106u 40u 13u "Browse..."
  Pop $btnBrowse

  ${If} $Portable == 1
    ${NSD_Check} $rbPortable
  ${Else}
    ${NSD_Check} $rbInstalled
  ${EndIf}
  ${If} $DoMenu == 1
    ${NSD_Check} $cbMenu
  ${EndIf}
  ${If} $DoDesktop == 1
    ${NSD_Check} $cbDesktop
  ${EndIf}
  ${NSD_OnClick} $rbInstalled OnModeClick
  ${NSD_OnClick} $rbPortable OnModeClick
  ${NSD_OnClick} $btnBrowse OnBrowse
  nsDialogs::Show
FunctionEnd

; Any folder on any drive is fine. If it is not writable for this user (Program Files, a drive root...),
; offer to restart the installer as administrator with the same choices.
Function ModeLeave
  ${NSD_GetState} $rbPortable $Portable
  ${NSD_GetState} $cbMenu $DoMenu
  ${NSD_GetState} $cbDesktop $DoDesktop
  ${NSD_GetText} $txtDir $INSTDIR
  ${If} $INSTDIR == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Please choose an install folder."
    Abort
  ${EndIf}
  Call CheckDir
  Pop $0
  ${If} $0 != 1
    Abort
  ${EndIf}
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  ; The program cannot be replaced while it is running.
  lockcheck:
  ClearErrors
  ${If} ${FileExists} "$INSTDIR\${EXE}"
    FileOpen $0 "$INSTDIR\${EXE}" a
    ${If} ${Errors}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "${APPNAME} is running. Close it, then press Retry." IDRETRY lockcheck
      Abort
    ${EndIf}
    FileClose $0
  ${EndIf}
  File "..\build_static\${EXE}"
  ${If} $Portable == 1
    FileOpen $0 "$INSTDIR\portable.flag" w
    FileWrite $0 "Settings, Redump data and downloaded tools live next to the exe while this file exists."
    FileClose $0
  ${Else}
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXE}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    ${If} $DoDesktop == 1
      CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXE}"
    ${EndIf}
    WriteRegStr HKCU "${UNKEY}" "DisplayName" "${APPNAME}"
    WriteRegStr HKCU "${UNKEY}" "DisplayIcon" "$INSTDIR\${EXE}"
    WriteRegStr HKCU "${UNKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKCU "${UNKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNKEY}" "NoRepair" 1
    ${If} $DoMenu == 1
      ExecWait '"$INSTDIR\${EXE}" install-menu'
    ${EndIf}
  ${EndIf}
SectionEnd

Function un.onInit
  ${un.GetParameters} $0
  ${un.GetOptions} $0 "/ELEVATED" $1
  ${If} ${Errors}
    ClearErrors
    FileOpen $0 "$INSTDIR\.psxrdc2_write_test" w
    ${If} ${Errors}
      ClearErrors
      ExecShell "runas" "$EXEPATH" "/ELEVATED _?=$INSTDIR"
      Quit
    ${EndIf}
    FileClose $0
    Delete "$INSTDIR\.psxrdc2_write_test"
  ${EndIf}
FunctionEnd

Section "Uninstall"
  ExecWait '"$INSTDIR\${EXE}" uninstall-menu'
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"
  DeleteRegKey HKCU "${UNKEY}"
  IfFileExists "$LOCALAPPDATA\psxrdc2\*.*" 0 done
    MessageBox MB_YESNO|MB_DEFBUTTON2 "Also delete your settings, Redump data and downloaded tools in $LOCALAPPDATA\psxrdc2? (Your dumps are never touched.)" IDNO done
    RMDir /r "$LOCALAPPDATA\psxrdc2"
  done:
SectionEnd
