param([switch]$SkipBuild, [string]$Output = 'dist/development/kithara-nea')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = [IO.Path]::GetFullPath((Join-Path $taskRoot $Output))
$taskDist = [IO.Path]::GetFullPath((Join-Path $taskRoot 'dist')) + [IO.Path]::DirectorySeparatorChar
if (-not $taskOutput.StartsWith($taskDist, [StringComparison]::OrdinalIgnoreCase)) { throw 'La livraison doit rester dans dist.' }
if (-not $SkipBuild) {
    $taskCMakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($taskCMakeCommand) { $taskCMake = $taskCMakeCommand.Source } else {
        $taskPackages = Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages'
        $taskCMake = Get-ChildItem -Path (Join-Path $taskPackages 'Kitware.CMake_*') -Filter cmake.exe -File -Recurse | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $taskCMake) { throw 'CMake et Visual Studio C++ sont necessaires.' }
    & $taskCMake -S $taskRoot -B (Join-Path $taskRoot 'build-juce') -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' '-DCMAKE_CONFIGURATION_TYPES=Release'
    if ($LASTEXITCODE) { throw 'Configuration CMake en echec.' }
    & $taskCMake --build (Join-Path $taskRoot 'build-juce') --config Release --target KitharaNea kithara-lv-native --parallel 4
    if ($LASTEXITCODE) { throw 'Compilation en echec.' }
    $taskHarmony = Join-Path $taskRoot 'resources/lv_chordia_native'
    Copy-Item -LiteralPath (Join-Path $taskRoot 'build-juce/bin/Release/kithara-lv-native.exe') -Destination $taskHarmony -Force
    $taskHarmonyManifest = Get-Content -LiteralPath (Join-Path $taskHarmony 'manifest.json') -Raw | ConvertFrom-Json -AsHashtable
    foreach ($taskFile in $taskHarmonyManifest.files) {
        if ($taskFile.path -eq 'kithara-lv-native.exe') {
            $taskBuilt = Get-Item -LiteralPath (Join-Path $taskHarmony $taskFile.path)
            $taskFile.bytes = $taskBuilt.Length
            $taskFile.sha256 = (Get-FileHash -LiteralPath $taskBuilt.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $taskHarmonyManifest.total_bytes = ($taskHarmonyManifest.files | Measure-Object -Property bytes -Sum).Sum
    $taskHarmonyManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskHarmony 'manifest.json') -Encoding utf8NoBOM
}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskBinary = Join-Path $taskRoot 'build-juce/KitharaNea_artefacts/Release/KITHÁRA NÉA.exe'
Copy-Item -LiteralPath $taskBinary -Destination (Join-Path $taskOutput 'kithara-nea.exe') -Force
foreach ($taskDocument in @('LICENSE_KITHARA_NEA.md','CONTRIBUTING.md','LICENSE_KITHARA_NEA_EN.md')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $taskDocument) -Destination $taskOutput -Force
}
$taskResources = Join-Path $taskOutput 'resources'
New-Item -ItemType Directory -Path $taskResources -Force | Out-Null
foreach ($taskPart in @('independent','bin','fluidsynth','lv_chordia_native','native-tests','locales','manuals','legal')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "resources/$taskPart") -Destination $taskResources -Recurse -Force
}
$taskWhisper = Join-Path $taskResources 'whisper'
New-Item -ItemType Directory -Force -Path (Join-Path $taskWhisper 'models') | Out-Null
foreach ($taskWhisperPart in @('bin','licenses')) {Copy-Item -LiteralPath (Join-Path $taskRoot "resources/whisper/$taskWhisperPart") -Destination $taskWhisper -Recurse -Force}
foreach ($taskWhisperFile in @('manifest-sha256.json','optional-models.json','NOTICE.txt')) {Copy-Item -LiteralPath (Join-Path $taskRoot "resources/whisper/$taskWhisperFile") -Destination $taskWhisper -Force}
Copy-Item -LiteralPath (Join-Path $taskRoot 'resources/whisper/models/ggml-small.bin') -Destination (Join-Path $taskWhisper 'models') -Force
if (Test-Path -LiteralPath (Join-Path $taskWhisper 'models/ggml-medium.bin')) {throw 'Medium ne doit pas etre inclus dans cette livraison.'}
$taskNotices = Join-Path $taskResources 'notices'
New-Item -ItemType Directory -Path $taskNotices -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'resources/notices') -File | Copy-Item -Destination $taskNotices -Force
foreach ($taskNoticePart in @('fluidsynth','libsndfile','ffmpeg')) { Copy-Item -LiteralPath (Join-Path $taskRoot "resources/notices/$taskNoticePart") -Destination $taskNotices -Recurse -Force }
$taskBeat = Join-Path $taskResources 'models/beat_this_official'
New-Item -ItemType Directory -Path $taskBeat -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'resources/models/beat_this_official') -File | Where-Object {$_.Extension -in '.onnx','.json','.md','.txt'} | Copy-Item -Destination $taskBeat -Force
foreach ($taskDll in @('soxr.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll')) {
    $taskSource = Join-Path $taskRoot "resources/lv_chordia_native/$taskDll"
    if (Test-Path -LiteralPath $taskSource) {Copy-Item -LiteralPath $taskSource -Destination $taskOutput -Force}
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'native/vendor/JUCE/LICENSE.md') -Destination (Join-Path $taskResources 'notices/JUCE-LICENSE.md') -Force
$taskForbidden = Get-ChildItem -LiteralPath $taskOutput -Recurse -File | Where-Object {$_.Extension -in '.py','.pyc','.pyd','.ckpt' -or $_.Name -match '^(python|torch|pyside)'}
if ($taskForbidden) { throw 'Une dependance Python ou un poids brut se trouve dans la livraison native.' }
$taskRows = @(Get-ChildItem -LiteralPath $taskOutput -Recurse -File | Where-Object {$_.Name -ne 'manifest-native.json'} | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($taskOutput,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
[ordered]@{schemaVersion=1;runtime='C++17 / JUCE';pythonRuntime=$false;files=$taskRows} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'manifest-native.json') -Encoding utf8NoBOM
Write-Output (Join-Path $taskOutput 'kithara-nea.exe')
