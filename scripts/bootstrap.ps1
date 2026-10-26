$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent

function Get-TaskNugetPackage([string]$Id,[string]$Version,[string]$Folder) {
    $taskNugetRoot=Join-Path $taskRoot '.deps\nuget'
    $taskDestination=Join-Path $taskNugetRoot $Folder
    if(Test-Path -LiteralPath (Join-Path $taskDestination "$Id.nuspec")){return}
    New-Item -Path $taskNugetRoot -ItemType Directory -Force | Out-Null
    $taskArchive=Join-Path $taskNugetRoot "$Folder.nupkg"
    $taskLower=$Id.ToLowerInvariant()
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$taskLower/$Version/$taskLower.$Version.nupkg" -OutFile $taskArchive
    # Windows PowerShell 5 Expand-Archive rejects the .nupkg extension.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($taskArchive,$taskDestination)
}
Get-TaskNugetPackage 'Microsoft.WindowsAppSDK.Widgets' '2.0.5' 'widgets'
Get-TaskNugetPackage 'Microsoft.Windows.CppWinRT' '3.0.260818.1' 'cppwinrt'
Get-TaskNugetPackage 'Microsoft.Web.WebView2' '1.0.4258.31' 'webview2'
$taskGenerated=Join-Path $taskRoot 'build\generated'
if(-not(Test-Path -LiteralPath (Join-Path $taskGenerated 'winrt\Microsoft.Windows.Widgets.Providers.h'))){
    & (Join-Path $taskRoot '.deps\nuget\cppwinrt\bin\cppwinrt.exe') -input sdk -input (Join-Path $taskRoot '.deps\nuget\widgets\metadata\Microsoft.Windows.Widgets.winmd') -output $taskGenerated
    if($LASTEXITCODE -ne 0){throw 'C++/WinRT projection generation failed.'}
}
