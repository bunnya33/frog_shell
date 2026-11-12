#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef PortableExe
  #error PortableExe is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef AppIcon
  #error AppIcon is required
#endif

[Setup]
; This identity must remain stable across releases and upgrades.
AppId={{B865FC75-30B0-47B9-8E44-7135B722F042}
AppName=FrogShell
AppVersion={#AppVersion}
AppPublisher=FrogShell
DefaultDirName={localappdata}\Programs\FrogShell
DefaultGroupName=FrogShell
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir={#OutputDir}
OutputBaseFilename=FrogShell-{#AppVersion}-setup
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\FrogShell.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=FrogShell
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=FrogShell Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=Before continuing, exit FrogShell from its system tray menu. Save any drafts and finish or cancel transfers first.%n%nSetup will not close FrogShell for you.
ErrorReplacingExistingFile=Could not replace the existing file. If FrogShell is running, exit it from the system tray after saving drafts and finishing transfers, then retry:
ErrorRenamingTemp=Could not rename a file in the destination directory. If FrogShell is running, exit it from the system tray after saving drafts and finishing transfers, then retry:

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#PortableExe}"; DestDir: "{app}"; DestName: "FrogShell.exe"; Flags: ignoreversion

[Icons]
Name: "{group}\FrogShell"; Filename: "{app}\FrogShell.exe"
Name: "{autodesktop}\FrogShell"; Filename: "{app}\FrogShell.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\FrogShell.exe"; Description: "Launch FrogShell"; Flags: nowait postinstall skipifsilent unchecked
