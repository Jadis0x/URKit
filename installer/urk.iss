; URKit installer. Build with Inno Setup 6:
;   ISCC.exe installer\urk.iss /DBinDir=C:\path\to\folder\with\urk.exe
; BinDir holds urk.exe and the DLLs of one release. The default is the staged public release.

#ifndef BinDir
  #define BinDir "..\out\build\msvc-release\public-release"
#endif

#define AppName "URKit"
#define FullVersion GetVersionNumbersString(BinDir + "\urk.exe")
; 0.5.0.0 -> 0.5.0
#if Copy(FullVersion, Len(FullVersion) - 1) == ".0"
  #define AppVersion Copy(FullVersion, 1, Len(FullVersion) - 2)
#else
  #define AppVersion FullVersion
#endif

[Setup]
AppId={{6F0E7E3A-5B1E-4C0A-9C57-2E4D2B8A7A31}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Jadis0x
AppPublisherURL=https://github.com/Jadis0x/URKit
AppSupportURL=https://github.com/Jadis0x/URKit/issues
AppUpdatesURL=https://github.com/Jadis0x/URKit/releases
VersionInfoVersion={#AppVersion}
; Per user: urk.exe replaces itself when it self-updates, which needs a folder the user can write to.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\src\app\urk.ico
UninstallDisplayIcon={app}\urk.exe
UninstallDisplayName={#AppName}
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
Compression=lzma2/max
SolidCompression=yes
LicenseFile=..\LICENSE
OutputDir=..\out\installer
OutputBaseFilename=URKit-Setup-{#AppVersion}
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "addtopath"; Description: "Add URKit to PATH, to run urk.exe from a terminal"; Flags: unchecked

[Files]
Source: "{#BinDir}\urk.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\version.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\winhttp.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\winmm.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\dwmapi.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\URKitInjector.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\URKitDevBridge.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\SDK_HANDBOOK.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\DEV_MCP.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\URKit"; Filename: "{app}\urk.exe"
Name: "{group}\Uninstall URKit"; Filename: "{uninstallexe}"
Name: "{autodesktop}\URKit"; Filename: "{app}\urk.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\urk.exe"; Description: "Open URKit"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Left behind by self-update.
Type: files; Name: "{app}\urk.exe.previous"
Type: files; Name: "{app}\urk.update-helper.exe"
; Settings and logs. Projects and the DLLs placed in game folders are not touched.
Type: filesandordirs; Name: "{localappdata}\URKit"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsUserPathEntry
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsMachinePathEntry

[Code]
const
  UserEnvironmentKey = 'Environment';
  MachineEnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function HasPathEntry(Root: Integer; Subkey: string): Boolean;
var
  Current: string;
begin
  if not RegQueryStringValue(Root, Subkey, 'Path', Current) then
    Current := '';
  Result := Pos(';' + Lowercase(ExpandConstant('{app}')) + ';', ';' + Lowercase(Current) + ';') > 0;
end;

function NeedsUserPathEntry: Boolean;
begin
  Result := (not IsAdminInstallMode) and (not HasPathEntry(HKCU, UserEnvironmentKey));
end;

function NeedsMachinePathEntry: Boolean;
begin
  Result := IsAdminInstallMode and (not HasPathEntry(HKLM, MachineEnvironmentKey));
end;

procedure RemovePathEntry(Root: Integer; Subkey: string);
var
  Current, Entry: string;
  P: Integer;
begin
  if not RegQueryStringValue(Root, Subkey, 'Path', Current) then
    Exit;
  Entry := ExpandConstant('{app}');
  P := Pos(';' + Lowercase(Entry) + ';', ';' + Lowercase(Current) + ';');
  if P = 0 then
    Exit;
  // Position P in the padded string is the same position in Current, minus the leading ';'.
  Delete(Current, P, Length(Entry) + 1);
  if (Length(Current) > 0) and (Current[Length(Current)] = ';') then
    Delete(Current, Length(Current), 1);
  RegWriteExpandStringValue(Root, Subkey, 'Path', Current);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  if IsAdminInstallMode then
    RemovePathEntry(HKLM, MachineEnvironmentKey)
  else
    RemovePathEntry(HKCU, UserEnvironmentKey);
end;
