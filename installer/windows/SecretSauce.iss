; The Secret Sauce — Windows installer (Inno Setup 6)
;
; Frictionless by design: no dongle, no activation, no account. Installs the
; VST3 to the standard system location and (optionally) the standalone app.
;
; Build (from the repo root, after a Release build):
;   iscc installer\windows\SecretSauce.iss
; Optional defines:
;   /DVersion=1.0.0
;   /DBuildDir=build\SecretSauce_artefacts\Release
;   /DOutputDir=build\installer

#ifndef Version
  #define Version "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\SecretSauce_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\installer"
#endif

#define AppName "The Secret Sauce"
#define Publisher "Secret Sauce Audio"

[Setup]
AppId={{7B1C93D4-5A2E-4C41-9E1F-5EC5A0CE1000}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#Publisher}
AppPublisherURL=https://github.com/jtekkk/the-secret-sauce-
DefaultDirName={autopf64}\{#Publisher}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=no
LicenseFile=..\..\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=TheSecretSauce-{#Version}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\The Secret Sauce.exe

[Components]
Name: "vst3";       Description: "VST3 plugin (recommended)";        Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application";           Types: full

[Files]
; JUCE produces a folder-format VST3 bundle on Windows — copy it whole into
; the standard system VST3 directory.
Source: "{#BuildDir}\VST3\The Secret Sauce.vst3\*"; \
    DestDir: "{commoncf64}\VST3\The Secret Sauce.vst3"; \
    Components: vst3; \
    Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#BuildDir}\Standalone\The Secret Sauce.exe"; \
    DestDir: "{app}"; \
    Components: standalone; \
    Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\The Secret Sauce.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}";  Filename: "{uninstallexe}"

[Run]
Filename: "{app}\The Secret Sauce.exe"; \
    Description: "Launch {#AppName}"; \
    Components: standalone; \
    Flags: nowait postinstall skipifsilent
