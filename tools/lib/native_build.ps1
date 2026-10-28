# Shared MSVC setup and staging for the DLL and native tests.
# Callers supply compiler commands; this helper stops on every failed step.

function Get-MsvcDeveloperCommand {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $VsWhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio C++ Build Tools.'
    }
    $VisualStudio = & $VsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$VisualStudio) {
        throw 'Visual Studio C++ Build Tools were not found.'
    }
    return Join-Path $VisualStudio 'Common7\Tools\VsDevCmd.bat'
}

function Initialize-NativeBuildStage {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][ValidateSet('native-build')][string]$Name
    )
    $Stage = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "local\$Name"))
    if (Test-Path -LiteralPath $Stage) {
        $ResolvedStage = (Resolve-Path -LiteralPath $Stage).Path
        if ($ResolvedStage -ne $Stage) {
            throw "Unexpected build staging path: $ResolvedStage"
        }
        Remove-Item -LiteralPath $ResolvedStage -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Stage -Force | Out-Null
    foreach ($Directory in @('include', 'src', 'vendor')) {
        Copy-Item -LiteralPath (Join-Path $ProjectRoot "native\$Directory") -Destination $Stage -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tests\native') -Destination (Join-Path $Stage 'tests') -Recurse
    return $Stage
}

function Get-NativeCompilerFlags {
    return '/nologo /utf-8 /std:c++20 /EHsc /O2 /MD /W4 /WX /permissive- /I include /I . /I tests'
}

function Invoke-MsvcBuild {
    param(
        [Parameter(Mandatory)][string]$StageDir,
        [Parameter(Mandatory)][string]$DeveloperCommand,
        [Parameter(Mandatory)][string[]]$Commands
    )
    $Lines = @(
        '@echo off'
        "call `"$DeveloperCommand`" -arch=x64 -host_arch=x64"
        'if errorlevel 1 exit /b %errorlevel%'
        'cd /d "%~dp0"'
        'if errorlevel 1 exit /b %errorlevel%'
    )
    foreach ($Command in $Commands) {
        $Lines += $Command
        $Lines += 'if errorlevel 1 exit /b %errorlevel%'
    }
    $BuildCmd = Join-Path $StageDir 'build.cmd'
    $Lines | Set-Content -LiteralPath $BuildCmd -Encoding ASCII
    & cmd.exe /d /c $BuildCmd
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed with exit code $LASTEXITCODE. Staging: $StageDir"
    }
}
