; =============================================================================
;  Inno Setup script for the SourceGlo Pro Windows installer.
;
;  Mirrors the macOS .pkg: the VST3 and the standalone are separate,
;  individually deselectable components.
;
;  Build with:  ISCC.exe packaging\windows\SourceGloPro.iss
;  Run from the project root, or pass /DSourceRoot=<path>.
; =============================================================================

#ifndef SourceRoot
  #define SourceRoot ".."
#endif

#define AppName        "SourceGlo Pro"
#define AppVersion     "1.0.0"
#define AppPublisher   "Diamond Loopz"
#define AppURL         "https://diamondloopz.com"
#define ArtefactDir    SourceRoot + "\..\build-cmake\SourceGloPro_artefacts\Release"

[Setup]
AppId={{9B4D2E17-5C83-42A6-8E14-SourceGlo0001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
OutputDir={#SourceRoot}\..\build\windows
OutputBaseFilename=SourceGloPro-{#AppVersion}-Windows
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 64-bit plug-in folders are outside the user's profile, so this needs elevation.
PrivilegesRequired=admin
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\windows\license.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in (Ableton Live, FL Studio, Cubase, Reaper, Studio One)"; Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone application (master a file without a DAW)";               Types: full custom; Flags: checkablealone

[Files]
; The VST3 is a folder bundle on Windows too, artwork included.
Source: "{#ArtefactDir}\VST3\{#AppName}.vst3\*"; \
    DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#ArtefactDir}\Standalone\{#AppName}.exe"; \
    DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; The standalone has no bundle, so its artwork sits beside the executable -
; the fallback Assets::assetsDirectory() checks after the bundle layout.
; Without this the app opens with every control drawn as a vector stand-in.
Source: "{#ArtefactDir}\Standalone\Assets\*"; \
    DestDir: "{app}\Assets"; \
    Components: standalone; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";       Filename: "{app}\{#AppName}.exe"; Components: standalone
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Components: standalone; Flags: unchecked

[Messages]
; This build is not signed with an Authenticode certificate, so SmartScreen will
; warn. Say so rather than letting it surprise anyone.
WelcomeLabel2=This will install [name/ver] on your computer.%n%nThis build is not code-signed, so Windows SmartScreen may warn you. Choose "More info" then "Run anyway" to continue.
