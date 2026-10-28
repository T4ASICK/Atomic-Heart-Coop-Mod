param([string]$GamePath='C:\Program Files (x86)\Steam\steamapps\common\Atomic Heart')
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$Win64=Join-Path $GamePath 'AtomicHeart\Binaries\Win64'
$State=Join-Path $Win64 'Mods\AHGhostCoop\state'
Write-Host "Game: $GamePath"
foreach ($Relative in @('Mods/AHGhostCoopNative/dlls/main.dll',
    'Mods/AHGhostCoop/bridge/server/ghost_coop/gui_service.py',
    'Mods/AHGhostCoop/bridge/runtime.ini','Mods/AHGhostCoop/bridge/runtime/steam/steam_api64.dll')) {
    Write-Host ("{0}: {1}" -f $Relative,$(if(Test-Path -LiteralPath (Join-Path $Win64 $Relative)){'present'}else{'MISSING - install the complete package'}))
}
$IncorrectDll=Join-Path $Win64 'Mods/AHGhostCoopNative/dlls/AHGhostCoopNative.dll'
if ((Test-Path -LiteralPath $IncorrectDll) -and !(Test-Path -LiteralPath (Join-Path $Win64 'Mods/AHGhostCoopNative/dlls/main.dll'))) {
    Write-Host 'Native mod filename is incorrect. UE4SS requires dlls/main.dll; use Install.cmd.'
}
$ThirdPartyClient=Join-Path $GamePath 'steamclient64.dll'
if (Test-Path -LiteralPath $ThirdPartyClient) {
    Write-Host ('Game-local Steam client product: '+(Get-Item -LiteralPath $ThirdPartyClient).VersionInfo.ProductName)
}
Get-Process -Name 'AtomicHeart-Win64-Shipping' -ErrorAction SilentlyContinue |
    Select-Object Id,Responding,StartTime | Format-Table -AutoSize
foreach ($Name in @('native_status.txt','native_performance.txt','bridge_status.txt','gui_status.tsv','steam_overlay_status.txt','steam_lobby.txt','local.tsv','remote.tsv')) {
    $Path=Join-Path $State $Name
    if (Test-Path -LiteralPath $Path) {
        $Age=((Get-Date)-(Get-Item -LiteralPath $Path).LastWriteTime).TotalSeconds
        Write-Host ("{0} (age {1:N1}s):" -f $Name,$Age)
        Get-Content -LiteralPath $Path -Encoding UTF8
    } else { Write-Host "$Name missing" }
}
$Installed=Join-Path $Win64 'Mods\AHGhostCoopNative\dlls\main.dll'
$Built=Join-Path $Root 'dist\AHGhostCoopNative.dll'
if ((Test-Path -LiteralPath $Installed) -and (Test-Path -LiteralPath $Built)) {
    Write-Host ('Installed DLL matches package: '+((Get-FileHash -LiteralPath $Installed).Hash -eq (Get-FileHash -LiteralPath $Built).Hash))
}
Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' } |
    Select-Object InterfaceAlias,IPAddress | Format-Table -AutoSize
Get-NetConnectionProfile -ErrorAction SilentlyContinue |
    Select-Object InterfaceAlias,NetworkCategory | Format-Table -AutoSize
$Log=Join-Path $Win64 'Mods\AHGhostCoopNative\native.log'
if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Tail 15 }
