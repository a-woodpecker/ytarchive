; Inno Setup script for YT Archive.
;
; Build with:  iscc /DAppVersion=0.2.2 packaging\ytarchive.iss
; Expects packaging\staging\ to hold a windeployqt-processed build. The
; build-installer.ps1 script in this folder does all of that for you.

#ifndef AppVersion
  #define AppVersion "0.2.2"
#endif

#define AppName       "YT Archive"
#define AppPublisher  "YT Archive"
#define AppExeName    "ytarchive.exe"
#define AppUrl        "https://github.com/a-woodpecker/ytarchive"

[Setup]
; Keep this GUID stable forever: it is how Windows recognises an upgrade of an
; existing install rather than a second, parallel copy.
AppId={{8D3C2A17-6F4B-4E29-9C8A-5B1D7E0A4F63}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
; The icon shown by the installer itself, and in Add/Remove Programs.
SetupIconFile=..\resources\ytarchive.ico
OutputDir=dist
OutputBaseFilename=YTArchive-{#AppVersion}-setup
LicenseFile=..\LICENSE.txt
InfoAfterFile=after-install.txt

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes

; 64-bit only, matching the x64 Qt build.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Offer a per-user install so administrator rights are not mandatory.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "staging\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Written by code below, so Inno does not track it automatically.
Type: files; Name: "{app}\defaults.ini"

[Code]
var
  ArchivePage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  ArchivePage := CreateInputDirPage(wpSelectDir,
    'Select Archive Folder',
    'Where should downloaded videos be stored?',
    'Videos are saved here in one subfolder per channel, alongside a catalog ' +
    'database. Pick a drive with plenty of free space - an archive grows quickly.' + #13#10 + #13#10 +
    'This folder is kept when you uninstall, and you can change it later in Preferences.',
    False, '');
  ArchivePage.Add('');
  ArchivePage.Values[0] := ExpandConstant('{%USERPROFILE}\Videos\Archive');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Path: String;
begin
  Result := True;
  if CurPageID = ArchivePage.ID then
  begin
    Path := Trim(ArchivePage.Values[0]);
    if Path = '' then
    begin
      MsgBox('Please choose a folder for the archive.', mbError, MB_OK);
      Result := False;
      Exit;
    end;
    // Fail here rather than after install, while the user can still correct it.
    if not ForceDirectories(Path) then
    begin
      MsgBox('That folder could not be created:' + #13#10 + Path + #13#10 + #13#10 +
             'Choose a different location.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function ArchiveRootForIni(Param: String): String;
begin
  // QSettings treats a backslash as an escape character when reading INI
  // values, so store the path with forward slashes. Qt accepts them on Windows.
  Result := ArchivePage.Values[0];
  StringChangeEx(Result, '\', '/', True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  IniPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    // The application reads this on first run, before any user settings exist.
    IniPath := ExpandConstant('{app}\defaults.ini');
    SaveStringToFile(IniPath,
      '[General]' + #13#10 +
      'archiveRoot=' + ArchiveRootForIni('') + #13#10, False);
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine +
            'Archive folder:' + NewLine + Space + ArchivePage.Values[0] + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + NewLine + MemoTasksInfo + NewLine;
end;
