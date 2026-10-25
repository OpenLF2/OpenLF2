; SPDX-License-Identifier: MIT
; Inno Setup script of the Windows installer (see dist/build-windows.ps1, which passes the /D defines):
;   AppVersion   project version, e.g. 1.1.0
;   Arch         x86, x64 or arm64
;   StageDir     the staged package: openlf2.exe, the DLLs, scripts\, licenses\, README.md, LICENSE
;   IconDir      tools/icons/make_icons.py output (openlf2.ico, windows\wizard*.bmp)
;   LanguageDir  the unofficial Chinese message files
;   OutputDir    where the installer goes
;   OutputBase   its name without .exe

#ifndef AppVersion
  #error AppVersion is not defined
#endif
#ifndef Arch
  #error Arch is not defined
#endif
#define AppName "OpenLF2"
#define AppExe "openlf2.exe"
#define RecordingId "OpenLF2.Recording"

[Setup]
; One id for every architecture, so a new installer updates an older one.
AppId={{7A1D5E6B-3C42-4B8E-9F1A-2D6C0E4B7A93}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=OpenLF2 contributors
AppPublisherURL=https://openlf2.github.io/OpenLF2/
AppSupportURL=https://github.com/OpenLF2/OpenLF2/issues
AppUpdatesURL=https://github.com/OpenLF2/OpenLF2/releases
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
; Per user by default (no administrator prompt, %LOCALAPPDATA%\Programs); the wizard offers "all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
#if Arch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#elif Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#endif
ChangesAssociations=yes
CloseApplications=yes
LicenseFile={#StageDir}\LICENSE
SetupIconFile={#IconDir}\openlf2.ico
WizardStyle=modern
WizardImageFile={#IconDir}\windows\wizard.bmp,{#IconDir}\windows\wizard-150.bmp,{#IconDir}\windows\wizard-200.bmp
WizardSmallImageFile={#IconDir}\windows\wizard-small.bmp,{#IconDir}\windows\wizard-small-150.bmp,{#IconDir}\windows\wizard-small-200.bmp
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBase}
Compression=lzma2/ultra64
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "chinesesimplified"; MessagesFile: "{#LanguageDir}\ChineseSimplified.isl"
Name: "chinesetraditional"; MessagesFile: "{#LanguageDir}\ChineseTraditional.isl"

[CustomMessages]
english.GameData=Game data:
english.DownloadLF2=Download the original Little Fighter 2 installer now (about 30 MB, required to play; OpenLF2 offers it at the first start otherwise)
english.FileTypes=File types:
english.AssociateRecordings=Open Little Fighter 2 recordings (.lfr) with OpenLF2
english.RecordingType=Little Fighter 2 recording
english.Downloading=Downloading Little Fighter 2
english.DownloadingDesc=Fetching LF2_v2.0a.exe (checked by its SHA-256 checksum).
english.DownloadFailed=The download failed or was canceled. The installation continues; OpenLF2 offers the download again at its first start.
english.RemoveUserData=Remove OpenLF2's settings, recordings and the downloaded game data from your user profile too?

german.GameData=Spieldaten:
german.DownloadLF2=Den Original-Installer von Little Fighter 2 jetzt herunterladen (ca. 30 MB, zum Spielen nötig; sonst bietet OpenLF2 es beim ersten Start an)
german.FileTypes=Dateitypen:
german.AssociateRecordings=Little-Fighter-2-Aufnahmen (.lfr) mit OpenLF2 öffnen
german.RecordingType=Little-Fighter-2-Aufnahme
german.Downloading=Little Fighter 2 wird heruntergeladen
german.DownloadingDesc=LF2_v2.0a.exe wird geladen (Prüfung per SHA-256-Prüfsumme).
german.DownloadFailed=Der Download ist fehlgeschlagen oder wurde abgebrochen. Die Installation wird fortgesetzt; OpenLF2 bietet den Download beim ersten Start erneut an.
german.RemoveUserData=Auch die Einstellungen, Aufnahmen und die heruntergeladenen Spieldaten von OpenLF2 aus deinem Benutzerprofil entfernen?

chinesesimplified.GameData=游戏数据：
chinesesimplified.DownloadLF2=立即下载原版小朋友齐打交安装程序（约 30 MB，游玩所需；否则 OpenLF2 会在首次启动时提供下载）
chinesesimplified.FileTypes=文件类型：
chinesesimplified.AssociateRecordings=用 OpenLF2 打开小朋友齐打交录像（.lfr）
chinesesimplified.RecordingType=小朋友齐打交录像
chinesesimplified.Downloading=正在下载小朋友齐打交
chinesesimplified.DownloadingDesc=正在获取 LF2_v2.0a.exe（以 SHA-256 校验和检查）。
chinesesimplified.DownloadFailed=下载失败或已取消。安装将继续；OpenLF2 会在首次启动时再次提供下载。
chinesesimplified.RemoveUserData=是否同时从用户配置文件中删除 OpenLF2 的设置、录像和已下载的游戏数据？

chinesetraditional.GameData=遊戲資料：
chinesetraditional.DownloadLF2=立即下載原版小朋友齊打交安裝程式（約 30 MB，遊玩所需；否則 OpenLF2 會在首次啟動時提供下載）
chinesetraditional.FileTypes=檔案類型：
chinesetraditional.AssociateRecordings=以 OpenLF2 開啟小朋友齊打交錄影檔（.lfr）
chinesetraditional.RecordingType=小朋友齊打交錄影檔
chinesetraditional.Downloading=正在下載小朋友齊打交
chinesetraditional.DownloadingDesc=正在取得 LF2_v2.0a.exe（以 SHA-256 檢查碼驗證）。
chinesetraditional.DownloadFailed=下載失敗或已取消。安裝將繼續；OpenLF2 會在首次啟動時再次提供下載。
chinesetraditional.RemoveUserData=是否同時從使用者設定檔中刪除 OpenLF2 的設定、錄影檔和已下載的遊戲資料？

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "{cm:AssociateRecordings}"; GroupDescription: "{cm:FileTypes}"
Name: "downloadlf2"; Description: "{cm:DownloadLF2}"; GroupDescription: "{cm:GameData}"; Check: NeedGameData

[Files]
Source: "{#StageDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\scripts\*"; DestDir: "{app}\scripts"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; The recording type: a ProgID that opens a file with `openlf2.exe -- FILE`. Registered for the user (or all users);
; the default program of .lfr is only taken when the task is ticked, "Open with" always lists OpenLF2.
Root: HKA; Subkey: "Software\Classes\{#RecordingId}"; ValueType: string; ValueName: ""; ValueData: "{cm:RecordingType}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#RecordingId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#RecordingId}\shell\open"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"
Root: HKA; Subkey: "Software\Classes\{#RecordingId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" -- ""%1"""
Root: HKA; Subkey: "Software\Classes\.lfr\OpenWithProgids"; ValueType: string; ValueName: "{#RecordingId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.lfr"; ValueType: string; ValueName: ""; ValueData: "{#RecordingId}"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.lfr"; ValueType: string; ValueName: "Content Type"; ValueData: "application/x-lf2-recording"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".lfr"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" -- ""%1"""

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  GameFile = 'LF2_v2.0a.exe';
  GameSha256 = '540c485547e234b0d09595f54d067aafd7176092c308aca61868f3cbc03030e1';
  // The sources OpenLF2 itself tries, in its order.
  GameUrl1 = 'https://archive.org/download/little-fighter-2_202404/Little%20Fighter%202.rar/Little%20Fighter%202%2FLF2Setup.exe';
  GameUrl2 = 'https://web.archive.org/web/20260921121026id_/https://lf2.net/LF2_v2.0a.exe';
  GameUrl3 = 'https://lf2.net/LF2_v2.0a.exe';

var
  DownloadPage: TDownloadWizardPage;
  GameDownloaded: Boolean;

// OpenLF2 keeps its configuration, recordings and the game installer in %APPDATA%\OpenLF2.
function UserDataDir: String;
begin
  Result := ExpandConstant('{userappdata}\OpenLF2');
end;

function NeedGameData: Boolean;
begin
  Result := not (FileExists(UserDataDir + '\' + GameFile) or FileExists(UserDataDir + '\lf2_v20a_Setup.exe'));
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(CustomMessage('Downloading'), CustomMessage('DownloadingDesc'), @OnDownloadProgress);
end;

// Tries the sources in turn; true when one delivered the file with the right checksum.
function DownloadGame: Boolean;
var
  Urls: array[0..2] of String;
  Index: Integer;
begin
  Result := False;
  Urls[0] := GameUrl1;
  Urls[1] := GameUrl2;
  Urls[2] := GameUrl3;
  for Index := 0 to 2 do
  begin
    DownloadPage.Clear;
    DownloadPage.Add(Urls[Index], GameFile, GameSha256);
    try
      DownloadPage.Download;
      Result := True;
      Exit;
    except
      Log('Download from ' + Urls[Index] + ' failed: ' + GetExceptionMessage);
      if DownloadPage.AbortedByUser then Exit;
    end;
  end;
end;

// The download is part of the wizard (after "Ready to install") and can be skipped by unticking its task; a failed
// or canceled download does not stop the installation.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and WizardIsTaskSelected('downloadlf2') then
  begin
    DownloadPage.Show;
    try
      GameDownloaded := DownloadGame;
      if not GameDownloaded and not DownloadPage.AbortedByUser then
        SuppressibleMsgBox(CustomMessage('DownloadFailed'), mbError, MB_OK, IDOK);
    finally
      DownloadPage.Hide;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then Exit;
  // A silent installation has no wizard pages: it downloads here (skip it with /TASKS="!downloadlf2").
  if WizardSilent and WizardIsTaskSelected('downloadlf2') then
  begin
    try
      DownloadTemporaryFile(GameUrl1, GameFile, GameSha256, nil);
      GameDownloaded := True;
    except
      Log('Silent download from ' + GameUrl1 + ' failed: ' + GetExceptionMessage);
      try
        DownloadTemporaryFile(GameUrl2, GameFile, GameSha256, nil);
        GameDownloaded := True;
      except
        Log('Silent download from ' + GameUrl2 + ' failed: ' + GetExceptionMessage);
        try
          DownloadTemporaryFile(GameUrl3, GameFile, GameSha256, nil);
          GameDownloaded := True;
        except
          Log('Silent download from ' + GameUrl3 + ' failed: ' + GetExceptionMessage);
        end;
      end;
    end;
  end;
  if GameDownloaded then
  begin
    ForceDirectories(UserDataDir);
    if not FileCopy(ExpandConstant('{tmp}\' + GameFile), UserDataDir + '\' + GameFile, False) then
      Log('Could not copy the downloaded game installer to ' + UserDataDir);
  end;
end;

// The uninstaller leaves what the user created alone unless asked.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and DirExists(UserDataDir) and not UninstallSilent then
    if MsgBox(CustomMessage('RemoveUserData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(UserDataDir, True, True, True);
end;
