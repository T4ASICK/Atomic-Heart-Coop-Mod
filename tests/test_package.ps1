$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$Temp=Join-Path $env:TEMP ('AHGhostCoop-package-test-'+[guid]::NewGuid().ToString('N'))
$Fixture=Join-Path $Temp 'source'
$Runtime=Join-Path $Temp 'python'
$Archive=Join-Path $Temp 'player.zip'
$SymbolsArchive=Join-Path $Temp 'player-symbols.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Assert($Value,[string]$Message) { if (!$Value) { throw $Message } }
function Assert-Rejected([scriptblock]$Command,[string]$Message) {
    $Rejected=$false
    try { & $Command | Out-Null } catch {
        $Rejected=$_.Exception.Message.Contains($Message)
        if (!$Rejected) { throw }
    }
    Assert $Rejected "Expected rejection: $Message"
}
function Write-FixtureFile([string]$Relative,[string]$Content='fixture') {
    $Path=Join-Path $Fixture $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Content)
}
function Read-ZipText($Zip,[string]$Name) {
    $Entry=$Zip.GetEntry($Name)
    Assert ($null -ne $Entry) "ZIP entry missing: $Name"
    $Reader=New-Object IO.StreamReader($Entry.Open())
    try { $Reader.ReadToEnd() } finally { $Reader.Dispose() }
}
function Assert-DllPdbPath([string]$Path) {
    # Read CodeView debug entries through the PE directory, not arbitrary strings.
    $Bytes=[IO.File]::ReadAllBytes($Path)
    $Pe=[BitConverter]::ToInt32($Bytes,0x3c)
    Assert ([Text.Encoding]::ASCII.GetString($Bytes,$Pe,4) -eq "PE`0`0") 'Not a PE DLL'
    $Optional=$Pe+24
    Assert ([BitConverter]::ToUInt16($Bytes,$Optional) -eq 0x20b) 'Expected an x64 DLL'
    $DebugRva=[BitConverter]::ToUInt32($Bytes,$Optional+112+6*8)
    $DebugSize=[BitConverter]::ToUInt32($Bytes,$Optional+112+6*8+4)
    $Sections=$Optional+[BitConverter]::ToUInt16($Bytes,$Pe+20)
    $DebugOffset=$null
    for ($Index=0; $Index -lt [BitConverter]::ToUInt16($Bytes,$Pe+6); $Index++) {
        $Section=$Sections+40*$Index
        $Rva=[BitConverter]::ToUInt32($Bytes,$Section+12)
        $Size=[Math]::Max([BitConverter]::ToUInt32($Bytes,$Section+8),[BitConverter]::ToUInt32($Bytes,$Section+16))
        if ($DebugRva -ge $Rva -and $DebugRva -lt $Rva+$Size) {
            $DebugOffset=[int]([BitConverter]::ToUInt32($Bytes,$Section+20)+$DebugRva-$Rva)
        }
    }
    Assert ($null -ne $DebugOffset) 'PE debug directory missing'
    $PdbPaths=@(for ($Offset=$DebugOffset; $Offset -lt $DebugOffset+$DebugSize; $Offset+=28) {
        if ([BitConverter]::ToUInt32($Bytes,$Offset+12) -ne 2) { continue }
        $Record=[BitConverter]::ToInt32($Bytes,$Offset+24)
        if ([Text.Encoding]::ASCII.GetString($Bytes,$Record,4) -ne 'RSDS') { continue }
        $Start=$Record+24
        $End=[Array]::IndexOf($Bytes,[byte]0,$Start)
        [Text.Encoding]::UTF8.GetString($Bytes,$Start,$End-$Start)
    })
    Assert ($PdbPaths.Count -eq 1 -and $PdbPaths[0] -ceq 'AHGhostCoopNative.pdb') 'DLL embeds a local or unexpected PDB path'
}
try {
    Assert-DllPdbPath (Join-Path $Root 'dist\AHGhostCoopNative.dll')
    New-Item -ItemType Directory -Path $Fixture,$Runtime -Force | Out-Null
    foreach ($Directory in @('tools','server','mod','assets')) {
        Copy-Item -LiteralPath (Join-Path $Root $Directory) -Destination $Fixture -Recurse
    }
    foreach ($Name in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','Install.cmd','Host.cmd','Join.cmd','Diagnostics.cmd','SteamHost.cmd','SteamJoin.cmd')) {
        Copy-Item -LiteralPath (Join-Path $Root $Name) -Destination $Fixture
    }
    foreach ($Relative in @('native/vendor/minhook-1.3.4/LICENSE.txt','native/vendor/quaternius/LICENSE.txt','native/vendor/quaternius/SOURCE.json')) {
        $Destination=Join-Path $Fixture $Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Root $Relative) -Destination $Destination
    }
    Write-FixtureFile 'dist/AHGhostCoopNavigation_P.pak' 'PAK fixture: tests packaging only, not Unreal cooking.'
    foreach ($Extension in @('dll','pdb','map')) {
        Copy-Item -LiteralPath (Join-Path $Root "dist\AHGhostCoopNative.$Extension") -Destination (Join-Path $Fixture 'dist')
    }
    foreach ($Relative in @('native/src/private.cpp','tests/private.ps1','logicmod/private.cpp','.editorconfig',
        'mod/AHGhostCoop/state/private.tsv','server/__pycache__/private.py','server/private.log','tools/private.ps1')) {
        Write-FixtureFile $Relative 'Must not enter the player archive'
    }
    # Synthetic Python files exercise layout validation without downloading dependencies in CI.
    foreach ($Name in @('python.exe','python3.dll','python313.dll','python313.zip','python313._pth','LICENSE.txt')) {
        [IO.File]::WriteAllText((Join-Path $Runtime $Name),'Python layout fixture')
    }
    Write-FixtureFile 'runtime/steam/steam_api64.dll' 'Steamworks fixture: packaging only'
    Write-FixtureFile 'runtime/steam/SOURCE.json' '{"source":"test fixture"}'
    Write-FixtureFile 'runtime/steam/Steamworks.NET-LICENSE.txt' 'Dependency license fixture'
    $Packager=Join-Path $Fixture 'tools\package.ps1'
    Assert-Rejected { & $Packager -ArchivePath $Archive } 'A player release requires'
    Assert-Rejected { & $Packager -PythonRuntimeSource $Runtime -PythonEmbedZip 'unused.zip' -ArchivePath $Archive } 'Choose PythonEmbedZip or PythonRuntimeSource'
    Move-Item -LiteralPath (Join-Path $Runtime 'LICENSE.txt') -Destination (Join-Path $Runtime 'LICENSE.saved')
    Assert-Rejected { & $Packager -PythonRuntimeSource $Runtime -ArchivePath $Archive } 'Portable Python runtime missing: LICENSE.txt'
    Move-Item -LiteralPath (Join-Path $Runtime 'LICENSE.saved') -Destination (Join-Path $Runtime 'LICENSE.txt')
    Move-Item -LiteralPath (Join-Path $Runtime 'python313.zip') -Destination (Join-Path $Runtime 'stdlib.saved')
    Assert-Rejected { & $Packager -PythonRuntimeSource $Runtime -ArchivePath $Archive } 'matching DLL, standard library ZIP and _pth'
    Move-Item -LiteralPath (Join-Path $Runtime 'stdlib.saved') -Destination (Join-Path $Runtime 'python313.zip')
    Assert (!(Test-Path -LiteralPath $Archive)) 'Invalid input produced a player ZIP'
    Assert (!(Test-Path -LiteralPath $SymbolsArchive)) 'Invalid input produced a symbols ZIP'

    & $Packager -PythonRuntimeSource $Runtime -ArchivePath $Archive | Out-Null
    $Zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $Entries=@($Zip.Entries | ForEach-Object { $_.FullName.Replace('\','/') } | Where-Object { !$_.EndsWith('/') })
        foreach ($Required in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','Install.cmd','Host.cmd','Join.cmd','Diagnostics.cmd',
            'SteamHost.cmd','SteamJoin.cmd','tools/setup_steam.ps1','server/ghost_coop/steam_api.py','server/ghost_coop/steam_bridge.py',
            'runtime/steam/steam_api64.dll','runtime/steam/ahg_steam_api64.dll','runtime/steam/SOURCE.json','runtime/steam/Steamworks.NET-LICENSE.txt',
            'dist/AHGhostCoopNative.dll','dist/AHGhostCoopNavigation_P.pak','runtime/python/python.exe','runtime/python/LICENSE.txt',
            'runtime/python/python3.dll','runtime/python/python313.dll','runtime/python/python313.zip','runtime/python/python313._pth',
            'mod/AHGhostCoop/settings.ini','mod/AHGhostCoop/Scripts/main.lua','tools/install.ps1','tools/start.ps1','tools/diagnose.ps1',
            'tools/firewall.ps1','assets/sponsor-ru.svg','assets/sponsor-en.svg','server/ah_ghost_relay.py','server/ghost_coop/cli.py',
            'licenses/MinHook-LICENSE.txt','licenses/Quaternius-LICENSE.txt','licenses/Quaternius-SOURCE.json')) {
            Assert ($Entries -contains "AHGhostCoop/$Required") "Player ZIP missing: $Required"
        }
        $Readme=Read-ZipText $Zip 'AHGhostCoop/README.md'
        foreach ($Image in [regex]::Matches($Readme,'!\[[^\]]*\]\((assets/[^)]+)\)')) {
            $ImagePath=$Image.Groups[1].Value
            Assert ($Entries -contains "AHGhostCoop/$ImagePath") "README image missing from player ZIP: $ImagePath"
        }
        Assert (!($Entries -match '/(native|tests|logicmod|__pycache__|state)/|\.(pdb|map|pyc|log)$|/private\.|/\.editorconfig$')) 'Development or state files entered the player ZIP'
        Assert (!($Entries -match '/tools/(?!install\.ps1$|start\.ps1$|diagnose\.ps1$|firewall\.ps1$|setup_steam\.ps1$)')) 'Development tools entered the player ZIP'
        Assert (!($Entries -match '(?i)/(UE4SS[^/]*|dwmapi\.dll|MemberVariableLayout\.ini|LICENSE-UE4SS\.txt)(/|$)')) 'UE4SS entered the player ZIP'
        $Notices=Read-ZipText $Zip 'AHGhostCoop/THIRD_PARTY_NOTICES.md'
        Assert (!$Notices.Contains('native/vendor/')) 'Packaged notices have broken vendor paths'
        foreach ($Name in @('MinHook-LICENSE.txt','Quaternius-LICENSE.txt','Quaternius-SOURCE.json')) {
            Assert ($Notices.Contains("licenses/$Name")) "Packaged notice link missing: $Name"
        }
        $ExtractedDll=Join-Path $Temp 'packaged.dll'
        [IO.Compression.ZipFileExtensions]::ExtractToFile($Zip.GetEntry('AHGhostCoop/dist/AHGhostCoopNative.dll'),$ExtractedDll)
        Assert-DllPdbPath $ExtractedDll
    } finally { $Zip.Dispose() }
    $Zip=[IO.Compression.ZipFile]::OpenRead($SymbolsArchive)
    try {
        Assert ($null -ne $Zip.GetEntry('AHGhostCoop-symbols/AHGhostCoopNative.pdb')) 'Separate PDB missing'
        Assert ($null -ne $Zip.GetEntry('AHGhostCoop-symbols/AHGhostCoopNative.map')) 'Separate MAP missing'
        $Hash=(Get-FileHash -LiteralPath $ExtractedDll -Algorithm SHA256).Hash
        Assert ((Read-ZipText $Zip 'AHGhostCoop-symbols/DLL-SHA256.txt').StartsWith($Hash)) 'Symbols refer to a different DLL'
    } finally { $Zip.Dispose() }

    $PlayerHash=(Get-FileHash -LiteralPath $Archive).Hash
    $SymbolsHash=(Get-FileHash -LiteralPath $SymbolsArchive).Hash
    [IO.File]::WriteAllText((Join-Path $Runtime 'UE4SS.dll'),'Unexpected loader')
    Assert-Rejected { & $Packager -PythonRuntimeSource $Runtime -ArchivePath $Archive } 'UE4SS files must not be bundled'
    Remove-Item -LiteralPath (Join-Path $Runtime 'UE4SS.dll')
    Assert ((Get-FileHash -LiteralPath $Archive).Hash -eq $PlayerHash) 'Failed packaging replaced the player release'
    Assert ((Get-FileHash -LiteralPath $SymbolsArchive).Hash -eq $SymbolsHash) 'Failed packaging replaced the symbols'

    $EmbedZip=Join-Path $Temp 'python-embed.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($Runtime,$EmbedZip)
    $CustomSymbols=Join-Path $Temp 'custom-symbols.zip'
    & $Packager -PythonEmbedZip $EmbedZip -ArchivePath (Join-Path $Temp 'embed-player.zip') -SymbolsArchivePath $CustomSymbols | Out-Null
    Assert (Test-Path -LiteralPath $CustomSymbols -PathType Leaf) 'Custom symbols archive missing'
    Write-Host 'Release tests passed: required Python and license, ZIP contents, separate matching symbols, DLL PDB path, UE4SS rejection and both Python inputs.'
} finally {
    $Resolved=[IO.Path]::GetFullPath($Temp)
    $Parent=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
    if (!$Resolved.StartsWith($Parent,[StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $Resolved) -notlike 'AHGhostCoop-package-test-*') { throw 'Unsafe fixture cleanup path' }
    if (Test-Path -LiteralPath $Resolved) { Remove-Item -LiteralPath $Resolved -Recurse -Force }
}
