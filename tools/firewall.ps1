# Run elevated on the host laptop only. No public-network or Internet access rule.
param([switch]$Remove)
$ErrorActionPreference='Stop'
$Name='AHGhostCoop-LAN-UDP-47777'
$Identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$Principal=New-Object Security.Principal.WindowsPrincipal($Identity)
if (!$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open PowerShell as administrator and run this script on the host laptop.'
}
$Existing=Get-NetFirewallRule -Name $Name -ErrorAction SilentlyContinue
if ($Remove) {
    if ($Existing) { $Existing | Remove-NetFirewallRule }
    return
}
if ($Existing) {
    Write-Host 'Ghost Co-op LAN rule already exists.'
    return
}
New-NetFirewallRule -Name $Name -DisplayName 'Atomic Heart Ghost Co-op (home LAN)' `
    -Direction Inbound -Action Allow -Protocol UDP -LocalPort 47777 `
    -Profile Private -RemoteAddress LocalSubnet | Out-Null
Write-Host 'Allowed UDP 47777 from the local subnet on Private networks only.'
