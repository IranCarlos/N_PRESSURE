; Instalador do aplicativo. Compilado por installer/build.py.
#define AppName "nPressure"

[Setup]
AppId={{7E3B6C1A-52D4-4F0E-9B1E-3D8A6F21C7A4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Dynaminds Electronics
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
; Sem exigir administrador: instala para o usuario, ou para todos se ele escolher.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
SetupIconFile={#BuildDir}\{#AppName}.ico
UninstallDisplayIcon={app}\{#AppName}.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na área de trabalho"

[Files]
Source: "{#BuildDir}\dist\{#AppName}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppName}.exe"
Name: "{group}\{#AppName} (demonstração, sem equipamento)"; Filename: "{app}\{#AppName}.exe"; Parameters: "--virtual"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Abrir o nPressure agora"; Flags: nowait postinstall skipifsilent
