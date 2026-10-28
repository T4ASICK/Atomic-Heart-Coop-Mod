param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][string]$UE4SSPath
)
$ErrorActionPreference='Stop'
$Temp=Join-Path $env:TEMP ('AHGhostCoop-install-test-'+[guid]::NewGuid().ToString('N'))
$Game=Join-Path $Temp 'SteamLibrary\Atomic Heart'
$Win64=Join-Path $Game 'AtomicHeart\Binaries\Win64'
$Mods=Join-Path $Win64 'Mods'
New-Item -ItemType Directory -Path $Mods -Force | Out-Null
New-Item -ItemType File -Path (Join-Path $Win64 'AtomicHeart-Win64-Shipping.exe') | Out-Null
[IO.File]::WriteAllText((Join-Path $Mods 'mods.txt'),"UnrelatedMod : 1`nAHGhostCoop : 0`n")
function Assert($Value,$Message) { if (!$Value) { throw $Message } }
# Isolate the installer's global running-game guard for this empty fixture only.
function Get-Process { [CmdletBinding()]param([string[]]$Name) }
try {
    $Installer=Join-Path $PackageRoot 'tools\install.ps1'
    $Rejected=$false
    try { & $Installer -GamePath $Game -DryRun } catch {
        $Rejected=$_.Exception.Message.Contains('Install UE4SS for Atomic Heart first')
    }
    Assert $Rejected 'Missing separately installed UE4SS was accepted'
    Assert (!(Test-Path -LiteralPath (Join-Path $Win64 'UE4SS.dll'))) 'Installer supplied UE4SS'
    Assert (!(Test-Path -LiteralPath (Join-Path $Win64 'AHGhostCoopBackups'))) 'Missing UE4SS check wrote files'
    # Simulate the user's separate UE4SS installation; it is not in PackageRoot.
    foreach ($Name in @('UE4SS.dll','UE4SS-settings.ini','MemberVariableLayout.ini','dwmapi.dll','UE4SS_Signatures')) {
        Copy-Item -LiteralPath (Join-Path $UE4SSPath $Name) -Destination $Win64 -Recurse
    }
    $RuntimeHashes=@{}
    foreach ($Name in @('UE4SS.dll','MemberVariableLayout.ini','dwmapi.dll')) {
        $RuntimeHashes[$Name]=(Get-FileHash -LiteralPath (Join-Path $Win64 $Name)).Hash
    }
    foreach ($File in Get-ChildItem -LiteralPath (Join-Path $Win64 'UE4SS_Signatures') -File -Recurse) {
        $Relative=$File.FullName.Substring($Win64.Length+1)
        $RuntimeHashes[$Relative]=(Get-FileHash -LiteralPath $File.FullName).Hash
    }
    $Layout=Join-Path $Win64 'MemberVariableLayout.ini'
    Move-Item -LiteralPath $Layout -Destination ($Layout+'.saved')
    try {
        $Rejected=$false
        try { & $Installer -GamePath $Game -DryRun } catch {
            $Rejected=$_.Exception.Message.Contains('UE4SS installation incomplete')
        }
        Assert $Rejected 'Incomplete UE4SS installation was accepted'
    } finally {
        Move-Item -LiteralPath ($Layout+'.saved') -Destination $Layout
    }
    & $Installer -GamePath $Game -DryRun
    Assert (!(Test-Path -LiteralPath (Join-Path $Mods 'AHGhostCoopNative\dlls\main.dll'))) 'Dry run installed the mod'
    & $Installer -GamePath $Game
    $Bridge=Join-Path $Mods 'AHGhostCoop\bridge'
    Assert (Test-Path -LiteralPath (Join-Path $Bridge 'server\ghost_coop\gui_service.py')) 'Installed menu service missing'
    Assert (Test-Path -LiteralPath (Join-Path $Bridge 'runtime.ini')) 'Installed menu Python configuration missing'
    Assert (!(Get-ChildItem -LiteralPath $Bridge -Recurse -File -Filter '*.pyc')) 'Python cache files entered the installation'
    $NavPak=Join-Path $Game 'AtomicHeart\Content\Paks\~mods\AHGhostCoopNavigation_P.pak'
    Assert (Test-Path -LiteralPath $NavPak) 'Navigation widget pak missing'
    $Config=Join-Path $Mods 'AHGhostCoop\settings.ini'
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=procedural_p3')) 'Default procedural model missing'
    [IO.File]::WriteAllText($Config,"[Visual]`nFeetOffset=-91`nMeshYawOffset=-90`n")
    $State=Join-Path $Mods 'AHGhostCoop\state'
    New-Item -ItemType Directory -Path $State -Force | Out-Null
    $Lock=[IO.File]::Open((Join-Path $State 'bridge.lock'),'OpenOrCreate','ReadWrite','ReadWrite')
    $Lock.WriteByte(48)
    $Lock.Lock(0,1)
    try { & $Installer -GamePath $Game } finally { $Lock.Dispose() }
    $Text=[IO.File]::ReadAllText((Join-Path $Mods 'mods.txt'))
    Assert ($Text.Contains('UnrelatedMod : 1')) 'Unrelated mod changed'
    Assert (($Text -split 'AHGhostCoop : 1').Count -eq 2) 'Duplicate mod enable entry'
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('FeetOffset=-91')) 'User settings changed'
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('MeshYawOffset=0')) 'Old yaw default not migrated'
    [IO.File]::WriteAllText($Config,"[Visual]`nFeetOffset=-91`nMeshYawOffset=15`n")
    & $Installer -GamePath $Game
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('MeshYawOffset=15')) 'Custom yaw correction changed'
    & $Installer -GamePath $Game -CharacterModel story_p3
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=story_p3')) 'Story P3 selection missing'
    & $Installer -GamePath $Game
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=procedural_p3')) 'Default reinstall did not select procedural model'
    & $Installer -GamePath $Game -CharacterModel procedural_p3
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=procedural_p3')) 'Procedural selection missing'
    & $Installer -GamePath $Game
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=procedural_p3')) 'Reinstall reset procedural model'
    & $Installer -GamePath $Game -CharacterModel runner
    Assert ((Get-Content -LiteralPath $Config -Raw).Contains('CharacterModel=runner')) 'Runner rollback failed'
    Assert (Test-Path -LiteralPath (Join-Path $Mods 'AHGhostCoopNative\dlls\main.dll')) 'Native DLL missing'
    foreach ($Extension in @('pdb','map')) {
        Assert (!(Test-Path -LiteralPath (Join-Path $Mods "AHGhostCoopNative\dlls\main.$Extension"))) 'Installer copied development symbols'
    }
    & $Installer -GamePath $Game -Uninstall
    Assert (!(Test-Path -LiteralPath $NavPak)) 'Navigation pak not removed on uninstall'
    Assert ((Get-Content -LiteralPath (Join-Path $Mods 'mods.txt') -Raw).Contains('AHGhostCoopNative : 0')) 'Uninstall failed'
    foreach ($Name in $RuntimeHashes.Keys) {
        Assert ((Get-FileHash -LiteralPath (Join-Path $Win64 $Name)).Hash -eq $RuntimeHashes[$Name]) "UE4SS dependency changed: $Name"
    }
    [IO.File]::WriteAllBytes((Join-Path $Win64 'UE4SS.dll'),[byte[]]@(1,2,3))
    $UnsupportedHash=(Get-FileHash -LiteralPath (Join-Path $Win64 'UE4SS.dll')).Hash
    $Rejected=$false
    try { & $Installer -GamePath $Game } catch { $Rejected=$_.Exception.Message.Contains('Unsupported UE4SS') }
    Assert $Rejected 'Unknown runtime was accepted'
    Assert ((Get-FileHash -LiteralPath (Join-Path $Win64 'UE4SS.dll')).Hash -eq $UnsupportedHash) 'Unsupported UE4SS was replaced'
    Write-Host 'Installer tests passed: missing/incomplete external UE4SS rejection, dry run, mod install/update, bridge lock, settings migration, character selection, uninstall, external UE4SS preservation and ABI rejection.'
} finally {
    $Resolved=[IO.Path]::GetFullPath($Temp)
    $Parent=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
    if (!$Resolved.StartsWith($Parent,[StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $Resolved) -notlike 'AHGhostCoop-install-test-*') { throw 'Unsafe fixture cleanup path' }
    Remove-Item -LiteralPath $Resolved -Recurse -Force
}
