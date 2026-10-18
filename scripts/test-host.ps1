$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$build = Join-Path $project 'build-host'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$gcc = Get-Command g++ -ErrorAction SilentlyContinue
if ($gcc) {
    & $gcc.Source -std=c++17 -Wall -Wextra -pedantic "-I$(Join-Path $project 'include')" `
        (Join-Path $project 'source\playlist.cpp') (Join-Path $project 'source\storage.cpp') `
        (Join-Path $project 'tests\test_core.cpp') -o (Join-Path $build 'core-tests.exe')
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'No se encontro g++ ni Visual Studio Build Tools.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio no incluye las herramientas C++ x64.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $command = 'call "{0}" >nul && cl /nologo /EHsc /std:c++17 /I"{1}" "{2}" "{3}" "{4}" /Fe:"{5}" /Fo:"{6}\\"' -f `
        $vcvars, (Join-Path $project 'include'), (Join-Path $project 'tests\test_core.cpp'), `
        (Join-Path $project 'source\playlist.cpp'), (Join-Path $project 'source\storage.cpp'), `
        (Join-Path $build 'core-tests.exe'), $build
    & cmd.exe /d /s /c $command
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $build 'core-tests.exe')
exit $LASTEXITCODE
