param(
    [string]$SteamworksSdk,
    [string]$SteamApiPath,
    [switch]$Download
)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
if ((@($SteamworksSdk,$SteamApiPath,$Download) | Where-Object { $_ }).Count -gt 1) {
    throw 'Choose Download, SteamworksSdk or SteamApiPath.'
}
$Runtime=Join-Path $Root 'runtime\steam'
if ($Download) {
    # Pinned public Steamworks.NET release, containing Valve's original native SDK library.
    $Url='https://github.com/rlabrecque/Steamworks.NET/releases/download/2025.164.1/Steamworks.NET-Standalone_2025.164.1.zip'
    $Expected='9412348cc404563be5a43a28347cfeda3c679ee044a14d87a507ed2d796a537d'
    $Cache=Join-Path $Root 'local\dependencies\Steamworks.NET-Standalone_2025.164.1.zip'
    New-Item -ItemType Directory -Path (Split-Path -Parent $Cache) -Force | Out-Null
    if (!(Test-Path -LiteralPath $Cache)) {
        [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Cache
    }
    if ((Get-FileHash -LiteralPath $Cache -Algorithm SHA256).Hash -ne $Expected) {
        throw "Steamworks.NET download hash mismatch: $Cache"
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Path $Runtime -Force | Out-Null
    $Zip=[IO.Compression.ZipFile]::OpenRead($Cache)
    try {
        foreach ($Pair in @(@('Windows-x64/steam_api64.dll','steam_api64.dll'),@('LICENSE.txt','Steamworks.NET-LICENSE.txt'))) {
            $Entry=$Zip.GetEntry($Pair[0])
            if (!$Entry) { throw "Steamworks.NET release missing $($Pair[0])" }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($Entry,(Join-Path $Runtime $Pair[1]),$true)
        }
    } finally { $Zip.Dispose() }
    [IO.File]::WriteAllText((Join-Path $Runtime 'SOURCE.json'),(@{
        source=$Url; archive_sha256=$Expected; app_id=480;
        dll_sha256=(Get-FileHash -LiteralPath (Join-Path $Runtime 'steam_api64.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    } | ConvertTo-Json))
    $SteamApiPath=Join-Path $Runtime 'steam_api64.dll'
}
if ($SteamworksSdk) { $SteamApiPath=Join-Path $SteamworksSdk 'redistributable_bin\win64\steam_api64.dll' }
if (!$SteamApiPath -or !(Test-Path -LiteralPath $SteamApiPath -PathType Leaf)) {
    throw 'Supply -Download, -SteamworksSdk <SDK sdk directory> or -SteamApiPath <official x64 steam_api64.dll>.'
}
$Bytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $SteamApiPath).Path)
if ($Bytes.Length -lt 256) { throw 'Invalid Steamworks DLL.' }
$Pe=[BitConverter]::ToInt32($Bytes,0x3c)
if ($Pe -lt 0 -or $Pe+26 -ge $Bytes.Length -or
    [Text.Encoding]::ASCII.GetString($Bytes,$Pe,4) -ne "PE`0`0" -or
    [BitConverter]::ToUInt16($Bytes,$Pe+4) -ne 0x8664) { throw 'Expected a Windows x64 Steamworks DLL.' }
$Destination=Join-Path $Root 'runtime\steam\steam_api64.dll'
New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
if ([IO.Path]::GetFullPath($SteamApiPath) -ne [IO.Path]::GetFullPath($Destination)) {
    Copy-Item -LiteralPath $SteamApiPath -Destination $Destination -Force
}
Write-Host "Steam Spacewar dependency ready: $Destination"
Copy-Item -LiteralPath $Destination -Destination (Join-Path $Runtime 'ahg_steam_api64.dll') -Force
Get-FileHash -LiteralPath $Destination -Algorithm SHA256
