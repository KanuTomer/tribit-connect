param([ValidateSet('Release','Debug')][string]$Configuration = 'Release',[switch]$Check)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools, Windows SDK, and CMake.' }
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio 2022 C++ build tools were not detected.' }
$taskCmake = Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $taskCmake)) { throw 'Install the Visual Studio C++ CMake component.' }
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $taskCmake --build (Join-Path $taskRoot 'build') --config $Configuration --target tribit-probe --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed.' }
if($Check){
    & $taskCmake --build (Join-Path $taskRoot 'build') --config $Configuration --target tribit-protocol-tests --parallel 2
    if($LASTEXITCODE -ne 0){throw 'Protocol test build failed.'}
    $taskCtest=Join-Path (Split-Path $taskCmake -Parent) 'ctest.exe'
    & $taskCtest --test-dir (Join-Path $taskRoot 'build') -C $Configuration --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'Protocol tests failed.'}
}
Write-Output (Join-Path $taskRoot "build\bin\$Configuration\tribit-probe.exe")
