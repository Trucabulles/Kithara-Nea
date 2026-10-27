param([string]$Output = 'dist/releases/0.2.1', [string]$Python = 'python', [string]$Iscc = '', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
& $Python (Join-Path $PSScriptRoot 'prepare_audio_sources.py')
if ($LASTEXITCODE) { throw 'Audio library sources could not be prepared.' }
$taskPackage = 'dist/release-work/kithara-nea'
& (Join-Path $PSScriptRoot 'build_juce.ps1') -Output $taskPackage -SkipBuild:$SkipBuild
if ($LASTEXITCODE) { throw 'Native build failed.' }
& $Python (Join-Path $PSScriptRoot 'stage_whisper.py') (Join-Path $taskRoot $taskPackage)
if ($LASTEXITCODE) { throw 'Whisper staging failed.' }
$taskArguments = @((Join-Path $PSScriptRoot 'package_release.py'), '--input', (Join-Path $taskRoot $taskPackage), '--output', (Join-Path $taskRoot $Output))
if ($Iscc) { $taskArguments += @('--iscc', $Iscc) }
& $Python @taskArguments
if ($LASTEXITCODE) { throw 'Release packaging failed.' }
