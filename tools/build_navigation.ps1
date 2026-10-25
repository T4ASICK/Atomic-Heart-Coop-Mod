param([string]$EnginePath='C:\Program Files\Epic Games\UE_4.27',[string]$WorkspaceRoot)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
if ($WorkspaceRoot) { $Root=$WorkspaceRoot }
$Stage=Join-Path $Root 'local\navigation-ui-build'
# UE4.27's compiler response files cannot reliably preserve Cyrillic PCH paths.
if ($Stage -match '[^\x00-\x7F]') {
    $Stage=Join-Path $env:LOCALAPPDATA 'AHGhostCoopBuild\navigation-ui-build'
}
if ($Stage -match '[^\x00-\x7F]') { throw 'UE4.27 requires an ASCII build staging path.' }
Write-Host "[AHGhostCoop] Resource build staging: $Stage"
New-Item -ItemType Directory -Path $Stage -Force | Out-Null
foreach ($Name in @('Config','Source','AHGhostCoopLogic.uproject')) {
    Copy-Item -LiteralPath (Join-Path $Root "logicmod\AHGhostCoopLogic\$Name") -Destination $Stage -Recurse -Force
}
$Project=Join-Path $Stage 'AHGhostCoopLogic.uproject'
$Editor=Join-Path $EnginePath 'Engine\Binaries\Win64\UE4Editor-Cmd.exe'
$Pak=Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealPak.exe'
# Installed-engine prerequisite shim, confined to this staging directory.
$env:UE_SDKS_ROOT=Join-Path $Stage 'Saved\AutoSDK'
$Sdk=Join-Path $env:UE_SDKS_ROOT 'HostWin64\Win64\Windows Kits\NETFXSDK\4.6'
New-Item -ItemType Directory -Path (Join-Path $Sdk 'Include\um'),(Join-Path $Sdk 'Lib\um\x64') -Force | Out-Null
if (!(Test-Path -LiteralPath (Join-Path $Sdk 'Include\um\mscoree.h'))) {
    New-Item -ItemType File -Path (Join-Path $Sdk 'Include\um\mscoree.h') | Out-Null
}
& (Join-Path $EnginePath 'Engine\Build\BatchFiles\Build.bat') AHGhostCoopLogicEditor Win64 Development "-Project=$Project" -WaitMutex
if ($LASTEXITCODE) { throw 'Editor module build failed' }
& $Editor $Project -run=AHGhostNavigationBuild -AllowCommandletRendering -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput
if ($LASTEXITCODE) { throw 'Navigation material generation failed' }
& $Editor $Project -run=Cook -TargetPlatform=WindowsNoEditor -CookDir=/Game/Mods/AHGhostCoopNavigation -unversioned -unattended -nop4 -stdout -FullStdOutLogOutput
if ($LASTEXITCODE) { throw 'Navigation material cook failed' }
$Response=Join-Path $Stage 'Saved\navigation-pak.txt'
$Lines=@()
foreach ($Name in @('WBP_GhostTag','WBP_GhostMenu','M_GhostWeaponOpaque')) {
    $Asset=Get-ChildItem -LiteralPath (Join-Path $Stage 'Saved\Cooked') -Recurse -File -Filter ($Name+'.uasset') | Select-Object -First 1
    if (!$Asset) { throw "Cooked asset missing: $Name" }
    foreach ($Extension in @('.uasset','.uexp','.ubulk')) {
        $File=[IO.Path]::ChangeExtension($Asset.FullName,$Extension)
        if (Test-Path -LiteralPath $File) { $Lines+='"'+$File+'" "../../../AtomicHeart/Content/Mods/AHGhostCoopNavigation/'+$Name+$Extension+'"' }
    }
}
$Lines | Set-Content -LiteralPath $Response -Encoding ASCII
$Output=Join-Path $Root 'dist\AHGhostCoopNavigation_P.pak'
& $Pak $Output "-Create=$Response"
if ($LASTEXITCODE) { throw 'Navigation pak build failed' }
& $Pak $Output -List
