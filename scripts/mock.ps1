# Rebuilds the browser mock of the widget from the real card code, in seconds instead of a repackage.
# Open build\mock\index.html afterwards (or refresh it if it is already open).
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskBuild=Join-Path $taskRoot 'build'
$taskMock=Join-Path $taskBuild 'mock'
if(-not(Test-Path -LiteralPath (Join-Path $taskBuild 'TribitConnect.sln'))){& (Join-Path $PSScriptRoot 'build.ps1');if($LASTEXITCODE){throw 'Initial build failed.'}}
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$taskCmake=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake --build $taskBuild --config Release --target widget-mock -- /nologo /verbosity:minimal
if($LASTEXITCODE -ne 0){throw 'Mock exporter build failed.'}
# Icons only change with the SVGs, so they are exported once and refreshed when an SVG is newer.
$taskIcons=Join-Path $taskMock 'icons'
$taskNewestSvg=(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'assets\icons') -Filter '*.svg' | Measure-Object LastWriteTime -Maximum).Maximum
$taskStamp=Join-Path $taskIcons '.exported'
if(-not(Test-Path -LiteralPath $taskStamp) -or (Get-Item -LiteralPath $taskStamp).LastWriteTime -lt $taskNewestSvg){
    & (Join-Path $PSScriptRoot 'export-icons.ps1') -Destination $taskIcons
    Set-Content -LiteralPath $taskStamp -Value (Get-Date -Format o)
}
$taskArt=Join-Path $taskMock 'art'
if(Test-Path -LiteralPath $taskArt){Get-ChildItem -LiteralPath $taskArt -Filter '*.png' | Remove-Item}
& (Join-Path $taskBuild 'bin\Release\widget-mock.exe') $taskMock
if($LASTEXITCODE -ne 0){throw 'Card export failed.'}
Copy-Item -LiteralPath (Join-Path $taskRoot 'tools\widget-mock.html') -Destination (Join-Path $taskMock 'index.html') -Force
Write-Output ('file:///'+((Join-Path $taskMock 'index.html') -replace '\\','/'))
