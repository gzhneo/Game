; Copyright (C) 2026 GZH Neo
; Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
; (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
;
; Inno Setup script of the player package. Built by the CMake target GZHNeoSetup, which fills the staging folder
; with cmake --install and passes:
;   /DStage=<staging folder>  /DVersion=<version text>  /DIcon=<program icon>
;   /DSuffix=-NoFreeContent   when built without the free content (GZH_INSTALL_FREE_CONTENT=OFF)
; The package holds no game data of EA: after the files are in place the setup runs GZHNeoEinrichtung.exe, which
; copies the data from the player's own installation into Generals\ and ZeroHour\ below the program folder.
; UTF-8 with byte order mark, as Inno Setup expects for texts outside ASCII.

#ifndef Stage
  #error Stage (staging folder from cmake --install) is not defined
#endif
#ifndef Version
  #define Version "0"
#endif
#ifndef Suffix
  #define Suffix ""
#endif

[Setup]
AppId={{77E67E45-55CC-4D71-8321-DE5792A224A2}
AppName=GZH Neo
AppVersion={#Version}
AppVerName=GZH Neo {#Version}
AppPublisher=GZH Neo
AppComments=Modified version of Command & Conquer Generals / Zero Hour; needs your own copy of the games.
DefaultDirName={localappdata}\Programs\GZH Neo
DefaultGroupName=GZH Neo
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
OutputBaseFilename=GZHNeo-{#Version}-Setup{#Suffix}
#ifdef Icon
SetupIconFile={#Icon}
#endif
UninstallDisplayIcon={app}\ZeroHour\GZHNeoZH.exe
UninstallDisplayName=GZH Neo {#Version}
Compression=lzma2
SolidCompression=yes
LZMAUseSeparateProcess=yes
; About 2.7 GB of game data are copied after the installation.
ExtraDiskSpaceRequired=2900000000

[Languages]
; Instead of a licence page (the GPL needs no acceptance) a short notice: what GZH Neo is, own copy of the game needed.
Name: "de"; MessagesFile: "compiler:Languages\German.isl"; InfoBeforeFile: "SetupNotice_de.txt"
Name: "en"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "SetupNotice_en.txt"

[CustomMessages]
de.SetUpData=Spieldaten einrichten
en.SetUpData=Set up game data
de.DataMissing=Die Spieldaten sind noch nicht eingerichtet. Ohne sie starten die Spiele nicht.%n%nDas lässt sich jederzeit nachholen: Startmenü, GZH Neo, „Spieldaten einrichten“.
en.DataMissing=The game data is not set up yet. Without it the games do not start.%n%nYou can do this at any time: Start menu, GZH Neo, "Set up game data".

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\GZH Neo\GZH Neo - Generals"; Filename: "{app}\Generals\GZHNeo.exe"; WorkingDir: "{app}\Generals"
Name: "{autoprograms}\GZH Neo\GZH Neo - Zero Hour"; Filename: "{app}\ZeroHour\GZHNeoZH.exe"; WorkingDir: "{app}\ZeroHour"
Name: "{autoprograms}\GZH Neo\{cm:SetUpData}"; Filename: "{app}\GZHNeoEinrichtung.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\GZH Neo\{cm:UninstallProgram,GZH Neo}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\GZH Neo - Generals"; Filename: "{app}\Generals\GZHNeo.exe"; WorkingDir: "{app}\Generals"; Tasks: desktopicon
Name: "{autodesktop}\GZH Neo - Zero Hour"; Filename: "{app}\ZeroHour\GZHNeoZH.exe"; WorkingDir: "{app}\ZeroHour"; Tasks: desktopicon

[Run]
Filename: "{app}\ZeroHour\GZHNeoZH.exe"; WorkingDir: "{app}\ZeroHour"; Description: "{cm:LaunchProgram,GZH Neo - Zero Hour}"; Flags: postinstall nowait skipifsilent unchecked; Check: DataReady

[UninstallDelete]
; The game data copied by GZHNeoEinrichtung.exe and what the games write next to themselves (crash reports). The
; user folder (settings, saved games) is in Documents\GZH Neo and stays.
Type: filesandordirs; Name: "{app}\Generals"
Type: filesandordirs; Name: "{app}\ZeroHour"
Type: files; Name: "{app}\GZHNeoEinrichtung.log"

[Code]
function DataReady(): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\Generals\GZHNeoData.json'))
    and FileExists(ExpandConstant('{app}\ZeroHour\GZHNeoData.json'));
end;

// Runs the setup window once the files are in place. Exit code 0: set up; otherwise a note how to do it later.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep = ssPostInstall) and not WizardSilent() then
  begin
    if not Exec(ExpandConstant('{app}\GZHNeoEinrichtung.exe'), '', ExpandConstant('{app}'), SW_SHOW,
      ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      MsgBox(CustomMessage('DataMissing'), mbInformation, MB_OK);
  end;
end;
