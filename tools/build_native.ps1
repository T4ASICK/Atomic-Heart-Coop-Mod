param(
    [switch]$Install,
    [switch]$Test,
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Atomic Heart'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\native_build.ps1')

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DeveloperCommand = Get-MsvcDeveloperCommand
$StageDir = Initialize-NativeBuildStage -ProjectRoot $ProjectRoot -Name 'native-build'
$DistDir = Join-Path $ProjectRoot 'dist'
$CompilerFlags = Get-NativeCompilerFlags
$Commands = @(
    'cl /nologo /c /O2 /MD /W3 vendor\minhook-1.3.4\src\buffer.c vendor\minhook-1.3.4\src\hook.c vendor\minhook-1.3.4\src\trampoline.c vendor\minhook-1.3.4\src\hde\hde64.c'
    "cl $CompilerFlags /Zi /DUNICODE /D_UNICODE src\dllmain.cpp buffer.obj hook.obj trampoline.obj hde64.obj user32.lib /link /DLL /DEBUG:FULL /OPT:REF /OPT:ICF /OUT:AHGhostCoopNative.dll /PDB:AHGhostCoopNative.pdb /PDBALTPATH:AHGhostCoopNative.pdb /MAP:AHGhostCoopNative.map"
)
if ($Test) {
    foreach ($Source in Get-ChildItem -LiteralPath (Join-Path $StageDir 'tests') -Filter '*_tests.cpp' -File | Sort-Object Name) {
        $Commands += "cl $CompilerFlags tests\$($Source.Name) /Fe:$($Source.BaseName).exe"
        $Commands += "$($Source.BaseName).exe"
    }
}

Write-Host '[AHGhostCoop] Building native bridge'
Invoke-MsvcBuild -StageDir $StageDir -DeveloperCommand $DeveloperCommand -Commands $Commands
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
foreach ($Extension in @('dll', 'pdb', 'map')) {
    Copy-Item -LiteralPath (Join-Path $StageDir "AHGhostCoopNative.$Extension") -Destination $DistDir -Force
}
Write-Host "[AHGhostCoop] Native bridge ready: $(Join-Path $DistDir 'AHGhostCoopNative.dll')"

if ($Install) {
    & (Join-Path $PSScriptRoot 'install.ps1') -GamePath $GamePath
}
