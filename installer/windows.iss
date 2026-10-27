#ifndef Payload
  #error Pass /DPayload=<installed payload directory>
#endif
#ifndef ReleaseOutput
  #error Pass /DReleaseOutput=<release directory>
#endif
#define AppVersion "0.2.1"
#ifndef LicenseRoot
  #error Pass /DLicenseRoot=<installer license directory>
#endif

[Setup]
AppId={{CBF45707-B73B-41A9-A747-C621C03AC1B8}
AppName=KITHÁRA NÉA
AppVersion={#AppVersion}
AppPublisher=Trucabulles
AppPublisherURL=https://github.com/Trucabulles/Kithara-Nea
DefaultDirName={autopf}\Kithara Nea
DefaultGroupName=KITHÁRA NÉA
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UsePreviousPrivileges=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#ReleaseOutput}
OutputBaseFilename=kithara-nea-{#AppVersion}-windows-x64-setup
SetupIconFile={#Payload}\resources\independent\brand\app.ico
UninstallDisplayIcon={app}\kithara-nea.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes
DisableProgramGroupPage=yes
UsePreviousLanguage=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "{#LicenseRoot}\license-en.txt"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"; LicenseFile: "{#LicenseRoot}\license-fr.txt"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\KITHÁRA NÉA"; Filename: "{app}\kithara-nea.exe"
Name: "{autodesktop}\KITHÁRA NÉA"; Filename: "{app}\kithara-nea.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\kithara-nea.exe"; Description: "{cm:LaunchProgram,KITHÁRA NÉA}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\installation-language.json"

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Destination: String;
begin
  Result := '';
  Destination := AddBackslash(ExpandFileName(WizardDirValue));
  if not IsAdminInstallMode then
    if (Pos(Lowercase(AddBackslash(ExpandConstant('{commonpf}'))), Lowercase(Destination)) = 1) or
       (Pos(Lowercase(AddBackslash(ExpandConstant('{commonpf32}'))), Lowercase(Destination)) = 1) then
    begin
      if ActiveLanguage = 'french' then
        Result := 'Ce dossier nécessite une installation pour tous les utilisateurs. Choisissez un dossier accessible dans votre profil, ou relancez l''installateur et choisissez tous les utilisateurs.'
      else
        Result := 'This folder requires an all-users installation. Choose a writable folder in your profile, or restart Setup and select all users.';
    end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  LanguageCode: String;
begin
  if CurStep = ssPostInstall then
  begin
    LanguageCode := 'en';
    if ActiveLanguage = 'french' then
      LanguageCode := 'fr';
    if not SaveStringToFile(ExpandConstant('{app}\installation-language.json'),
      '{"uiLanguage":"' + LanguageCode + '"}', False) then
      RaiseException('Unable to save application language.');
  end;
end;
