param(
    [ValidateSet('Host','Join')][string]$Role,
    [string]$Name,
    [string]$Room,
    [string]$Server,
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Atomic Heart'
)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent (Split-Path -Parent $PSCommandPath)
if (!$Role) { $Role=Read-Host 'Host or Join' }
if ($Role -notin @('Host','Join')) { throw 'Choose Host or Join' }
if (!$Name) { $Name=Read-Host 'Your player name' }
if (!$Room) { $Room=Read-Host 'Room code (same on both laptops, at least 4 characters)' }
$Python=Join-Path $Root 'runtime\python\python.exe'
if (!(Test-Path -LiteralPath $Python)) {
    $SystemPython=Get-Command python -ErrorAction SilentlyContinue
    if (!$SystemPython) { throw 'Python 3.10+ is required. Use the package with Python included or install Python.' }
    $Python=$SystemPython.Source
}
if (!(Test-Path -LiteralPath (Join-Path $GamePath 'AtomicHeart\Binaries\Win64\Mods\AHGhostCoopNative\dlls\main.dll'))) {
    throw 'Install the mod first, or supply -GamePath for your Steam library.'
}
$Arguments=@((Join-Path $Root 'server\ah_ghost_relay.py'))
if ($Role -eq 'Host') {
    $Arguments+='host'
    Write-Host 'Host IPv4 addresses (use the Wi-Fi adapter address on the other laptop):'
    Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' } |
        Select-Object InterfaceAlias,IPAddress | Format-Table -AutoSize
} else {
    if (!$Server) { $Server=Read-Host 'Host laptop IPv4 address' }
    $Arguments+=@('client','--server',$Server)
}
$Arguments+=@('--name',$Name,'--room',$Room,'--game-path',$GamePath)
& $Python @Arguments
if ($LASTEXITCODE -ne 0) { throw "Bridge exited with code $LASTEXITCODE" }
