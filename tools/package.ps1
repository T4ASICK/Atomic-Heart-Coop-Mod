param(
    [string]$PythonEmbedZip,
    [string]$PythonRuntimeSource,
    [string]$ArchivePath,
    [string]$SymbolsArchivePath
)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
if (!$PythonEmbedZip -and !$PythonRuntimeSource) {
    throw 'A player release requires PythonEmbedZip or PythonRuntimeSource, including the Python license.'
}
if ($PythonEmbedZip -and $PythonRuntimeSource) { throw 'Choose PythonEmbedZip or PythonRuntimeSource.' }

function Assert-PythonRuntime([string]$Directory) {
    foreach ($Name in @('python.exe','python3.dll','LICENSE.txt')) {
        if (!(Test-Path -LiteralPath (Join-Path $Directory $Name) -PathType Leaf)) {
            throw "Portable Python runtime missing: $Name"
        }
    }
    $VersionFiles=@(Get-ChildItem -LiteralPath $Directory -File -Filter 'python3*._pth' | Where-Object {
        $_.BaseName -match '^python3(\d+)$' -and [int]$Matches[1] -ge 10 -and
        (Test-Path -LiteralPath (Join-Path $Directory ($_.BaseName+'.dll')) -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $Directory ($_.BaseName+'.zip')) -PathType Leaf)
    })
    if (!$VersionFiles) { throw 'Portable Python 3.10+ requires matching DLL, standard library ZIP and _pth files.' }
}

if ($PythonRuntimeSource) { Assert-PythonRuntime $PythonRuntimeSource }
if ($PythonEmbedZip -and !(Test-Path -LiteralPath $PythonEmbedZip -PathType Leaf)) {
    throw 'Embeddable Python ZIP missing.'
}
foreach ($Name in @('AHGhostCoopNative.dll','AHGhostCoopNative.pdb','AHGhostCoopNative.map','AHGhostCoopNavigation_P.pak')) {
    if (!(Test-Path -LiteralPath (Join-Path $Root "dist\$Name") -PathType Leaf)) {
        throw "Build artifact missing: dist\$Name"
    }
}
$Archive=if ($ArchivePath) { [IO.Path]::GetFullPath($ArchivePath) } else { Join-Path $Root 'dist\AHGhostCoop-0.10.6-preview.zip' }
$SymbolsArchive=if ($SymbolsArchivePath) { [IO.Path]::GetFullPath($SymbolsArchivePath) } else {
    Join-Path (Split-Path -Parent $Archive) ([IO.Path]::GetFileNameWithoutExtension($Archive)+'-symbols.zip')
}
if ($Archive -eq $SymbolsArchive) { throw 'Player and symbols archives must have different paths.' }

$Stage=Join-Path $Root ('local\packages\'+[guid]::NewGuid().ToString('N'))
$Package=Join-Path $Stage 'AHGhostCoop'
New-Item -ItemType Directory -Path $Package -Force | Out-Null
# Explicit release inputs keep development tools, state and source files out.
$PlayerFiles=@(
    'README.md','LICENSE','THIRD_PARTY_NOTICES.md','Install.cmd','Host.cmd','Join.cmd','Diagnostics.cmd',
    'SteamHost.cmd','SteamJoin.cmd','tools/setup_steam.ps1',
    'assets/sponsor-ru.svg','assets/sponsor-en.svg',
    'assets/ui/steam-lobby.png','assets/ui/teleport-players.png',
    'tools/install.ps1','tools/start.ps1','tools/diagnose.ps1','tools/firewall.ps1',
    'mod/AHGhostCoop/settings.ini','mod/AHGhostCoop/Scripts/main.lua',
    'dist/AHGhostCoopNative.dll','dist/AHGhostCoopNavigation_P.pak'
)
foreach ($SteamFile in @('steam_api64.dll','SOURCE.json','Steamworks.NET-LICENSE.txt')) {
    $Relative='runtime/steam/'+$SteamFile
    if (Test-Path -LiteralPath (Join-Path $Root $Relative) -PathType Leaf) { $PlayerFiles+=$Relative }
}
$Gameplay=Join-Path $Root 'assets\gameplay'
if (Test-Path -LiteralPath $Gameplay -PathType Container) {
    foreach ($Gif in Get-ChildItem -LiteralPath $Gameplay -File -Filter '*.gif' | Sort-Object Name) {
        $PlayerFiles+='assets/gameplay/'+$Gif.Name
    }
}
foreach ($Relative in $PlayerFiles) {
    $Destination=Join-Path $Package $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Root $Relative) -Destination $Destination
}
$PackagedSteamApi=Join-Path $Package 'runtime/steam/steam_api64.dll'
if (Test-Path -LiteralPath $PackagedSteamApi) {
    Copy-Item -LiteralPath $PackagedSteamApi -Destination (Join-Path $Package 'runtime/steam/ahg_steam_api64.dll')
}
$Server=Join-Path $Root 'server'
foreach ($File in Get-ChildItem -LiteralPath $Server -File -Recurse -Filter '*.py') {
    if ($File.FullName -match '[\\/]__pycache__[\\/]') { continue }
    $Destination=Join-Path $Package ('server\'+$File.FullName.Substring($Server.Length+1))
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $File.FullName -Destination $Destination
}
$LicenseFiles=@{
    'native/vendor/minhook-1.3.4/LICENSE.txt'='licenses/MinHook-LICENSE.txt'
    'native/vendor/quaternius/LICENSE.txt'='licenses/Quaternius-LICENSE.txt'
    'native/vendor/quaternius/SOURCE.json'='licenses/Quaternius-SOURCE.json'
}
New-Item -ItemType Directory -Path (Join-Path $Package 'licenses') -Force | Out-Null
$NoticesPath=Join-Path $Package 'THIRD_PARTY_NOTICES.md'
$Notices=[IO.File]::ReadAllText($NoticesPath)
foreach ($Source in $LicenseFiles.Keys) {
    Copy-Item -LiteralPath (Join-Path $Root $Source) -Destination (Join-Path $Package $LicenseFiles[$Source])
    $Notices=$Notices.Replace($Source,$LicenseFiles[$Source])
}
[IO.File]::WriteAllText($NoticesPath,$Notices,(New-Object Text.UTF8Encoding($false)))
$Python=Join-Path $Package 'runtime\python'
if ($PythonEmbedZip) {
    Expand-Archive -LiteralPath $PythonEmbedZip -DestinationPath $Python
} else {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Python) -Force | Out-Null
    Copy-Item -LiteralPath $PythonRuntimeSource -Destination $Python -Recurse
}
Assert-PythonRuntime $Python
# UE4SS is a separately installed dependency and must never enter this archive.
$UE4SSFiles=@('UE4SS.dll','dwmapi.dll','UE4SS-settings.ini','MemberVariableLayout.ini','LICENSE-UE4SS.txt')
if (Get-ChildItem -LiteralPath $Package -Recurse -Force | Where-Object {
    $_.Name -in $UE4SSFiles -or $_.Name -in @('ue4ss','UE4SS_Signatures')
}) {
    throw 'UE4SS files must not be bundled. Download and install UE4SS separately.'
}
$Symbols=Join-Path $Stage 'AHGhostCoop-symbols'
New-Item -ItemType Directory -Path $Symbols -Force | Out-Null
foreach ($Extension in @('pdb','map')) {
    Copy-Item -LiteralPath (Join-Path $Root "dist\AHGhostCoopNative.$Extension") -Destination $Symbols
}
$DllHash=(Get-FileHash -LiteralPath (Join-Path $Package 'dist\AHGhostCoopNative.dll') -Algorithm SHA256).Hash
[IO.File]::WriteAllText((Join-Path $Symbols 'DLL-SHA256.txt'),"$DllHash  AHGhostCoopNative.dll`r`n")
[IO.File]::WriteAllText((Join-Path $Symbols 'README.txt'),
    "Optional full debugging symbols for the DLL identified in DLL-SHA256.txt.`r`n"+
    "Not required for installation. Full PDB files can retain source paths from the build machine.`r`n")

Add-Type -AssemblyName System.IO.Compression.FileSystem,System.IO.Compression
function Write-PackageArchive([string]$Directory,[string]$Destination) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    # ZipFile uses shared reads, avoiding Compress-Archive locks in synced folders.
    $TemporaryArchive=$Destination+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        $Zip=[IO.Compression.ZipFile]::Open($TemporaryArchive,[IO.Compression.ZipArchiveMode]::Create)
        try {
            $Prefix=Split-Path -Leaf $Directory
            foreach ($File in Get-ChildItem -LiteralPath $Directory -File -Recurse -Force) {
                $Relative=$File.FullName.Substring($Directory.Length+1).Replace('\','/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($Zip,$File.FullName,
                    "$Prefix/$Relative",[IO.Compression.CompressionLevel]::Optimal)
            }
        } finally { $Zip.Dispose() }
        Move-Item -LiteralPath $TemporaryArchive -Destination $Destination -Force
    } finally {
        if (Test-Path -LiteralPath $TemporaryArchive -PathType Leaf) {
            Remove-Item -LiteralPath $TemporaryArchive -Force
        }
    }
}
Write-PackageArchive $Symbols $SymbolsArchive
Write-PackageArchive $Package $Archive
Write-Host "Player package: $Archive"
Write-Host "Optional debugging symbols: $SymbolsArchive"
Write-Host "Staging copy: $Package"
Get-FileHash -LiteralPath $Archive,$SymbolsArchive -Algorithm SHA256
