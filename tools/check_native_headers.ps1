# Compile each public header alone to catch dependencies on include order.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\native_build.ps1')

$Root = Split-Path -Parent $PSScriptRoot
$IncludeRoot = (Resolve-Path -LiteralPath (Join-Path $Root 'native\include')).Path
$Stage = Join-Path $Root 'local\header-checks'
New-Item -ItemType Directory -Path $Stage -Force | Out-Null
$Commands = @()
$Headers = Get-ChildItem -LiteralPath $IncludeRoot -Recurse -Filter '*.hpp' -File | Sort-Object FullName
foreach ($Header in $Headers) {
    $Relative = $Header.FullName.Substring($IncludeRoot.Length + 1).Replace('\', '/')
    $CheckName = $Relative.Replace('/', '_').Replace('.hpp', '_check.cpp')
    [IO.File]::WriteAllText((Join-Path $Stage $CheckName), '#include "' + $Relative + '"' + [Environment]::NewLine)
    $Commands += 'cl /nologo /std:c++20 /EHsc /MD /W4 /WX /permissive- /Zs /TP /I "..\..\native\include" /I "..\..\native" ' + $CheckName
}
Invoke-MsvcBuild -StageDir $Stage -DeveloperCommand (Get-MsvcDeveloperCommand) -Commands $Commands
Write-Host "[AHGhostCoop] All $($Headers.Count) public headers compile independently."
