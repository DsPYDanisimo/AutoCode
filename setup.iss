[Setup]
AppName=ECU Diagnostic System
AppVersion=1.0
DefaultDirName={autopf}\ECU_Diagnostic
DefaultGroupName=ECU Diagnostic
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=installer
OutputBaseFilename=ECU_Diagnostic_Setup
SetupIconFile=icon.ico

[Files]
; ќсновные файлы
Source: "install.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "run.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "docker-compose.yml"; DestDir: "{app}"; Flags: ignoreversion
Source: "Dockerfile"; DestDir: "{app}"; Flags: ignoreversion
Source: "back\*"; DestDir: "{app}\back"; Flags: ignoreversion recursesubdirs
Source: "front\*"; DestDir: "{app}\front"; Flags: ignoreversion recursesubdirs
Source: "tools\*"; DestDir: "{app}\tools"; Flags: ignoreversion recursesubdirs

; ярлыки
[Icons]
Name: "{group}\ECU Diagnostic"; Filename: "{app}\run.bat"; IconFilename: "{app}\icon.ico"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{commondesktop}\ECU Diagnostic"; Filename: "{app}\run.bat"; IconFilename: "{app}\icon.ico"

[Run]
Filename: "{app}\install.bat"; Description: "Install dependencies"; Flags: runascurrentuser waituntilterminated
Filename: "{app}\run.bat"; Description: "Launch ECU Diagnostic"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "docker"; Parameters: "compose down"; WorkingDir: "{app}"; Flags: runhidden
Filename: "taskkill"; Parameters: "/F /IM socat.exe"; Flags: runhidden