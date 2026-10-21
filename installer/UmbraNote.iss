#define MyAppName "UmbraNote"
#define MyAppVersion "1.2.0"
#define MyAppExeName "UmbraNote.exe"
[Setup]
AppId={{AF3C14F9-2E61-40D7-8D97-68318F180B03}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=UmbraNote
DefaultDirName={localappdata}\Programs\UmbraNote
DefaultGroupName=UmbraNote
OutputDir=..\release\windows
OutputBaseFilename=UmbraNote-Setup-1.2.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
Uninstallable=yes

[Files]
Source: "..\releases\windows\UmbraNote.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\UmbraNote"; Filename: "{app}\UmbraNote.exe"
