param(
    [string]$GamePath = "C:\Program Files (x86)\Steam\steamapps\common\Atomic Heart",
    [switch]$Uninstall,
    [switch]$DryRun,
    [ValidateSet('runner','story_p3','procedural_p3')][string]$CharacterModel='procedural_p3'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$Win64 = [IO.Path]::GetFullPath((Join-Path $GamePath 'AtomicHeart\Binaries\Win64'))
$Mods = Join-Path $Win64 'Mods'
$Settings = Join-Path $Win64 'UE4SS-settings.ini'
$ModsTxt = Join-Path $Mods 'mods.txt'
$Dll = Join-Path $ProjectRoot 'dist\AHGhostCoopNative.dll'
$NavigationPak = Join-Path $ProjectRoot 'dist\AHGhostCoopNavigation_P.pak'
$InstalledPak = Join-Path $GamePath 'AtomicHeart\Content\Paks\~mods\AHGhostCoopNavigation_P.pak'
$SupportedHash = 'A10A74378D90588FADE051AF00D0D4A76AAF87969DB262110031E2DEF05BA336'
$RuntimeDll = Join-Path $Win64 'UE4SS.dll'
if (!(Test-Path -LiteralPath (Join-Path $Win64 'AtomicHeart-Win64-Shipping.exe'))) {
    throw "Atomic Heart executable not found under $Win64"
}
if (Get-Process -Name 'AtomicHeart-Win64-Shipping','CrashReportClient' -ErrorAction SilentlyContinue) {
    throw 'Close Atomic Heart and its Crash Reporter before installing. No running process was stopped.'
}
if (!$Uninstall) {
    if (!(Test-Path -LiteralPath $Dll)) { throw 'Build the native DLL first: tools\build_native.ps1' }
    if (!(Test-Path -LiteralPath $NavigationPak)) { throw 'Build the navigation material first: tools\build_navigation.ps1' }
    if (!(Test-Path -LiteralPath $RuntimeDll)) {
        throw 'Install UE4SS for Atomic Heart first: https://www.nexusmods.com/atomicheart/mods/186'
    }
    if ((Get-FileHash -LiteralPath $RuntimeDll -Algorithm SHA256).Hash -ne $SupportedHash) {
        throw 'Unsupported UE4SS binary. This mod is ABI-specific; your runtime was not overwritten.'
    }
    foreach ($Name in @('UE4SS-settings.ini','MemberVariableLayout.ini','dwmapi.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $Win64 $Name) -PathType Leaf)) {
            throw "UE4SS installation incomplete; install UE4SS separately. Missing file: $Name"
        }
    }
    if (!(Test-Path -LiteralPath (Join-Path $Win64 'UE4SS_Signatures') -PathType Container)) {
        throw 'UE4SS installation incomplete; install the Atomic Heart UE4SS_Signatures folder separately.'
    }
}
if ($DryRun) {
    Write-Host "Target: $Win64"
    Write-Host "Native DLL: $Dll"
    Write-Host "Uninstall: $Uninstall"
    return
}

# Keep rollback copies outside Mods so UE4SS cannot load them as extra mods.
$Backup = Join-Path $Win64 ('AHGhostCoopBackups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff'))
New-Item -ItemType Directory -Path $Backup -Force | Out-Null
foreach ($Name in @('AHGhostCoop','AHGhostCoopNative')) {
    $Target = Join-Path $Mods $Name
    if (Test-Path -LiteralPath $Target) {
        $Saved=Join-Path $Backup $Name
        New-Item -ItemType Directory -Path $Saved -Force | Out-Null
        # Volatile bridge state includes an exclusively locked single-instance file.
        Get-ChildItem -LiteralPath $Target | Where-Object { $_.Name -ne 'state' } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Saved -Recurse }
    }
}
foreach ($File in @($ModsTxt,$Settings)) {
    if (Test-Path -LiteralPath $File) { Copy-Item -LiteralPath $File -Destination $Backup }
}
New-Item -ItemType Directory -Path $Mods -Force | Out-Null
$Lines = if (Test-Path -LiteralPath $ModsTxt) { @(Get-Content -LiteralPath $ModsTxt) } else { @() }
foreach ($Name in @('AHGhostCoop','AHGhostCoopNative')) {
    $Lines = @($Lines | Where-Object { $_ -notmatch "^\s*$Name\s*:" })
    $Lines += "$Name : $(if($Uninstall){0}else{1})"
}

if ($Uninstall) {
    if (Test-Path -LiteralPath $InstalledPak) { Move-Item -LiteralPath $InstalledPak -Destination $Backup }
    # Disabling both entries also avoids deleting diagnostics or user configuration.
    Set-Content -LiteralPath $ModsTxt -Value $Lines -Encoding ASCII
    Write-Host "Both Ghost Co-op mods disabled. Backup: $Backup"
    return
}
$Lua = Join-Path $Mods 'AHGhostCoop'
$Native = Join-Path $Mods 'AHGhostCoopNative\dlls'
New-Item -ItemType Directory -Path $Lua,$Native -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'mod\AHGhostCoop\Scripts') -Destination $Lua -Recurse -Force
if (!(Test-Path -LiteralPath (Join-Path $Lua 'settings.ini'))) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'mod\AHGhostCoop\settings.ini') -Destination $Lua
}
Copy-Item -LiteralPath $Dll -Destination (Join-Path $Native 'main.dll') -Force
if (Test-Path -LiteralPath $InstalledPak) { Copy-Item -LiteralPath $InstalledPak -Destination $Backup }
New-Item -ItemType Directory -Path (Split-Path -Parent $InstalledPak) -Force | Out-Null
Copy-Item -LiteralPath $NavigationPak -Destination $InstalledPak -Force
Set-Content -LiteralPath $ModsTxt -Value $Lines -Encoding ASCII

if (!('AHGhostIni' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;
public static class AHGhostIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    public static extern int GetPrivateProfileInt(string section, string key, int fallback, string path);
}
'@
}
$VisualSettings=Join-Path $Lua 'settings.ini'
if ($CharacterModel) {
    if (![AHGhostIni]::WritePrivateProfileString('Visual','CharacterModel',$CharacterModel,$VisualSettings)) {
        throw 'Could not set the selected character model.'
    }
}
if ([AHGhostIni]::GetPrivateProfileInt('Visual','MeshYawOffset',0,$VisualSettings) -eq -90) {
    if (![AHGhostIni]::WritePrivateProfileString('Visual','MeshYawOffset','0',$VisualSettings)) {
        throw 'Could not migrate the old runner yaw correction.'
    }
}
foreach ($Entry in @(
    @('Hooks','HookEngineTick','1'),
    @('Hooks','HookGameViewportClientTick','1'),
    @('General','EnableHotReloadSystem','0'),
    @('Debug','ConsoleEnabled','0'),
    @('Debug','GuiConsoleEnabled','0')
)) {
    if (![AHGhostIni]::WritePrivateProfileString($Entry[0],$Entry[1],$Entry[2],$Settings)) {
        throw "Failed to update UE4SS settings. Backup: $Backup"
    }
}
$OldPak = Join-Path $GamePath 'AtomicHeart\Content\Paks\LogicMods\AHGhostCoop.pak'
if (Test-Path -LiteralPath $OldPak) {
    Move-Item -LiteralPath $OldPak -Destination (Join-Path $Backup 'AHGhostCoop.pak')
}
Write-Host "Installed Ghost Co-op 0.9.2-preview. Backup: $Backup"
Write-Host 'Controls: Home opens/closes the mod menu; online peers appear automatically.'
