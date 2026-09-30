; DNF点将工具 安装包脚本（Inno Setup 6）
; 编译：ISCC.exe scripts\installer\DNFGameCapture-setup.iss
; 只打包运行必需文件；卸载时彻底清理程序目录、用户数据、临时文件和授权信息（保留试用期记录）。

#define AppTitle "DNF点将工具"
#define AppVersion "5.5.4"
#define AppExe "DNFGameCapture.exe"
#define ProjDir AddBackslash(SourcePath) + "..\.."
#define ReleaseDir ProjDir + "\..\x64\Release"
#define WebDir ProjDir + "\web前端"

[Setup]
; 与旧安装包相同的 AppId：安装时直接覆盖升级旧版本，卸载入口只有一个
AppId={{B0AAE1D3-6ACF-4BBE-B9B7-4D7C0D6D6B12}
AppName={#AppTitle}
AppVersion={#AppVersion}
AppVerName={#AppTitle} {#AppVersion}
AppPublisher={#AppTitle}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppTitle}
DefaultDirName={localappdata}\DNFGameCapture
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#ProjDir}\deployment-packages
OutputBaseFilename={#AppTitle}-{#AppVersion}-Setup
SetupIconFile={#ProjDir}\res\DNFGameCapture.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppTitle} {#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
; 运行中的程序由 [Code] 负责检测并关闭
CloseApplications=no
RestartApplications=no
ChangesAssociations=no

[Languages]
Name: "chs"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："

[InstallDelete]
; 旧版本遗留、新版本已不再使用的文件
Type: files; Name: "{app}\web前端\graphite-theme.css"

[Files]
; ---- 主程序 ----
Source: "{#ReleaseDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\7za.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\sprite(击杀大XX).NPK"; DestDir: "{app}"; Flags: ignoreversion
; ---- 界面（直接取源码目录，保证与仓库一致）----
Source: "{#WebDir}\index.html"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\main.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\style.css"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\autocomplete-worker.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\kill.html"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\kill.css"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\kill.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\scene-rules.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\scene-rules-ui.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\scene-rules-ui.css"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\keys.html"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\keys.css"; DestDir: "{app}\web前端"; Flags: ignoreversion
Source: "{#WebDir}\keys.js"; DestDir: "{app}\web前端"; Flags: ignoreversion
; ---- 击杀语音播报：预生成的音色文件（web前端\voice\<音色>\<词>.wav）----
Source: "{#WebDir}\voice\*.wav"; DestDir: "{app}\web前端\voice"; Flags: ignoreversion recursesubdirs createallsubdirs
; ---- OCR 引擎（不含日志、临时文件和 Python 缓存）----
Source: "{#ReleaseDir}\Umi-OCR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleaseDir}\UmiOCR-data\*"; DestDir: "{app}\UmiOCR-data"; Excludes: "\logs\*,\temp_doc\*,__pycache__,*.pyc,*.log"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppTitle}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppTitle}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "启动 {#AppTitle}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
const
  AppExeName = '{#AppExe}';
  RegKey = 'Software\DNFCapture';
  WebView2ClientKey = 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}';

var
  AppDirOwned: Boolean;

{ ---------- 通用工具 ---------- }

function PsQuote(const S: String): String;
begin
  Result := S;
  StringChangeEx(Result, '''', '''''', True);
  Result := '''' + Result + '''';
end;

function RunPowerShell(const Script: String): Integer;
var
  RC: Integer;
begin
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + Script + '"',
    '', SW_HIDE, ewWaitUntilTerminated, RC) then
    RC := -1;
  Result := RC;
end;

{ 程序目录下是否有进程在运行（主程序、Umi-OCR 及其子进程）。只按路径判断，不会误伤用户自己装的 Umi-OCR }
function AppProcessesRunning(const Dir: String): Boolean;
var
  RC: Integer;
begin
  RC := RunPowerShell('if (Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith(' +
    PsQuote(AddBackslash(Dir)) + ', [StringComparison]::OrdinalIgnoreCase) }) { exit 1 } else { exit 0 }');
  Result := (RC = 1);
end;

procedure KillAppProcesses(const Dir: String);
var
  RC: Integer;
begin
  { 先按进程树结束主程序（连带 WebView2 子进程），再清理目录下剩余的进程（Umi-OCR 等） }
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /T /IM "' + AppExeName + '"', '', SW_HIDE, ewWaitUntilTerminated, RC);
  RunPowerShell('Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith(' +
    PsQuote(AddBackslash(Dir)) + ', [StringComparison]::OrdinalIgnoreCase) } | Stop-Process -Force -ErrorAction SilentlyContinue');
  Sleep(1500);
end;

function EnsureAppClosed(const Dir: String; Silent: Boolean): Boolean;
begin
  Result := True;
  if not DirExists(Dir) then Exit;
  if not AppProcessesRunning(Dir) then Exit;
  if not Silent then
    if MsgBox('检测到 {#AppTitle} 正在运行，需要先关闭才能继续。' + #13#10#13#10 +
      '点击“确定”自动关闭程序（未保存的比赛数据会丢失），点击“取消”退出。',
      mbConfirmation, MB_OKCANCEL) <> IDOK then
    begin
      Result := False;
      Exit;
    end;
  KillAppProcesses(Dir);
end;

function IsDirEmpty(const Dir: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := True;
  if FindFirst(AddBackslash(Dir) + '*', FindRec) then
  try
    repeat
      if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
      begin
        Result := False;
        Break;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function IsUnsafeDir(const Dir: String): Boolean;
var
  D: String;
begin
  D := Lowercase(RemoveBackslashUnlessRoot(Dir));
  Result := (Length(D) <= 3) or
    (D = Lowercase(ExpandConstant('{localappdata}'))) or
    (D = Lowercase(ExpandConstant('{userappdata}'))) or
    (D = Lowercase(ExpandConstant('{userdocs}'))) or
    (D = Lowercase(ExpandConstant('{userdesktop}'))) or
    (D = Lowercase(ExpandConstant('{userprograms}'))) or
    (D = Lowercase(ExpandConstant('{win}'))) or
    (D = Lowercase(ExpandConstant('{commonpf64}'))) or
    (D = Lowercase(ExpandConstant('{commonpf32}'))) or
    (D = Lowercase(GetEnv('USERPROFILE')));
end;

{ 删除 用户临时目录 下 Prefix* 形式的文件夹 }
procedure DeleteTempDirs(const Prefix: String);
var
  TempDir: String;
  FindRec: TFindRec;
begin
  TempDir := GetEnv('TEMP');
  if TempDir = '' then Exit;
  if FindFirst(AddBackslash(TempDir) + Prefix + '*', FindRec) then
  try
    repeat
      if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
        DelTree(AddBackslash(TempDir) + FindRec.Name, True, True, True);
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function WebView2Installed: Boolean;
var
  V: String;
begin
  Result :=
    (RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', V) and (V <> '') and (V <> '0.0.0.0')) or
    (RegQueryStringValue(HKLM, WebView2ClientKey, 'pv', V) and (V <> '') and (V <> '0.0.0.0')) or
    (RegQueryStringValue(HKCU, WebView2ClientKey, 'pv', V) and (V <> '') and (V <> '0.0.0.0'));
end;

{ ---------- 安装 ---------- }

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := RemoveBackslashUnlessRoot(WizardDirValue);
    { 卸载会删除整个安装目录，所以只允许装到空文件夹或原安装目录 }
    if IsUnsafeDir(Dir) then
    begin
      MsgBox('不能直接安装到磁盘根目录或系统/用户目录，请在其下新建一个文件夹（例如 ...\DNFGameCapture）。', mbError, MB_OK);
      Result := False;
    end
    else if DirExists(Dir) and (not FileExists(AddBackslash(Dir) + AppExeName)) and (not IsDirEmpty(Dir)) then
    begin
      MsgBox('所选文件夹不是空的。' + #13#10 +
        '卸载时会删除整个安装目录，为避免误删其它文件，请选择一个空文件夹或原来的安装目录。', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not EnsureAppClosed(ExpandConstant('{app}'), WizardSilent) then
    Result := '{#AppTitle} 仍在运行，安装已取消。';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not WizardSilent) and (not WebView2Installed) then
    MsgBox('未检测到 Microsoft Edge WebView2 运行时，程序界面需要它才能显示。' + #13#10 +
      '请到 https://go.microsoft.com/fwlink/p/?LinkId=2124703 下载安装（Windows 10/11 通常已自带）。',
      mbInformation, MB_OK);
end;

{ ---------- 卸载：彻底清理 ---------- }

function InitializeUninstall: Boolean;
begin
  Result := EnsureAppClosed(ExpandConstant('{app}'), UninstallSilent);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDir: String;
begin
  AppDir := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  case CurUninstallStep of
    usUninstall:
      { 确认安装目录确实是本程序的目录，才允许整目录删除 }
      AppDirOwned := (not IsUnsafeDir(AppDir)) and FileExists(AddBackslash(AppDir) + AppExeName);
    usPostUninstall:
      begin
        { 1. 安装目录：配置、授权文件、别名库、日志、比赛输出、更新包、WebView2 缓存、崩溃转储、OCR 设置等全部删除 }
        if AppDirOwned then
          DelTree(AppDir, True, True, True);
        { 2. %APPDATA%\DNFGameCapture：选手库数据库 }
        DelTree(ExpandConstant('{userappdata}\DNFGameCapture'), True, True, True);
        { 3. 临时目录：WebView2 配置文件夹、更新暂存目录、更新检查文件 }
        DeleteTempDirs('DNFGameCapture-WebView2-');
        DeleteTempDirs('DNFGameCapture-update-');
        DeleteFile(AddBackslash(GetEnv('TEMP')) + 'update_check.txt');
        { 4. 注册表：删除授权码和授权租约；保留 InstallTime / LastRun（试用期记录），防止重装重置试用 }
        RegDeleteValue(HKCU, RegKey, 'LicenseKey');
        RegDeleteValue(HKCU, RegKey, 'LicenseLeaseV1');
      end;
  end;
end;
