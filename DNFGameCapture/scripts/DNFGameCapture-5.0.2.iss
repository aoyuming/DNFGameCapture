#define AppTitle "DNF点将计分器"
#define AppVersion "5.2.0"
#define ReleaseDir "C:\Users\BRO\source\repos\DNFGameCapture\x64\Release"
#define PackageDir "C:\Users\BRO\source\repos\DNFGameCapture\DNFGameCapture\deployment-packages"

[Setup]
AppId={{B0AAE1D3-6ACF-4BBE-B9B7-4D7C0D6D6B12}
AppName={#AppTitle}
AppVersion={#AppVersion}
AppVerName={#AppTitle} {#AppVersion}
AppPublisher=DNF点将计分器
DefaultDirName={localappdata}\DNFGameCapture
DefaultGroupName={#AppTitle}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#PackageDir}
OutputBaseFilename=DNF点将计分器-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppTitle} {#AppVersion}
CloseApplications=no
RestartApplications=no
ChangesAssociations=no

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
Source: "{#ReleaseDir}\DNFGameCapture.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\Umi-OCR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\UmiOCR-data\*"; DestDir: "{app}\UmiOCR-data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ReleaseDir}\web前端\*"; DestDir: "{app}\web前端"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ReleaseDir}\sprite(击杀大XX).NPK"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\7za.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autodesktop}\{#AppTitle}"; Filename: "{app}\DNFGameCapture.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\DNFGameCapture.exe"; Description: "启动{#AppTitle}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\crash-dumps"
