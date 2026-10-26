param([switch]$Register)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskVersion=([xml](Get-Content -LiteralPath (Join-Path $taskRoot 'packaging\AppxManifest.xml') -Raw)).Package.Identity.Version
$taskExe=Join-Path $taskRoot 'build\bin\Release\TribitConnect.exe'
if(-not(Test-Path -LiteralPath $taskExe)){throw 'Run scripts/build.ps1 first.'}
# Development registrations run from out\package; release builds stage separately, because Windows
# keeps the registered package's files (its resources.pri) open while it is installed.
$taskStage=Join-Path $taskRoot $(if($Register){'out\package'}else{'out\release'})
$taskAssets=Join-Path $taskStage 'Assets'
New-Item -Path $taskAssets,(Join-Path $taskStage 'Public') -ItemType Directory -Force | Out-Null
# A registered development build runs from this folder; stop only that copy before replacing it.
$taskStaged=Join-Path $taskStage 'TribitConnect.exe'
foreach($taskProcess in @(Get-Process -Name TribitConnect -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $taskStaged})){
    Stop-Process -InputObject $taskProcess -Force
    if(-not $taskProcess.WaitForExit(5000)){throw 'The staged Tribit Connect did not exit; close it and retry.'}
}
Copy-Item -LiteralPath $taskExe -Destination $taskStage -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'assets/icons/LUCIDE-LICENSE.txt') -Destination $taskStage -Force
Copy-Item -LiteralPath (Join-Path $taskRoot '.deps\nuget\widgets\license.txt') -Destination (Join-Path $taskStage 'WindowsAppSDK-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'packaging\AppxManifest.xml') -Destination $taskStage -Force

# The Sound Lab frontend (WebView2): only what the page loads, not the design boards or tests.
$taskApp=Join-Path $taskStage 'app'
if(Test-Path -LiteralPath $taskApp){Remove-Item -LiteralPath $taskApp -Recurse -Force}
foreach($taskDir in 'js','styles','fonts'){New-Item -Path (Join-Path $taskApp $taskDir) -ItemType Directory -Force | Out-Null}
$taskSource=Join-Path $taskRoot 'app'
Copy-Item -LiteralPath (Join-Path $taskSource 'index.html') -Destination $taskApp
Copy-Item -Path (Join-Path $taskSource 'js\*.js') -Destination (Join-Path $taskApp 'js')
Copy-Item -LiteralPath (Join-Path $taskSource 'styles\app.css') -Destination (Join-Path $taskApp 'styles')
foreach($taskFont in 'archivo-latin-wdth-normal.woff2','doto-latin-800-normal.woff2','DSEG7Classic-Bold.woff2','archivo-LICENSE.txt','doto-LICENSE.txt','dseg-LICENSE.txt'){
    Copy-Item -LiteralPath (Join-Path $taskSource "fonts\$taskFont") -Destination (Join-Path $taskApp 'fonts')
}

# Generate application icons and a clearly illustrative picker preview.
Add-Type -AssemblyName System.Drawing
$taskIcons=Join-Path $taskStage 'Public\Icons'
if(Test-Path -LiteralPath $taskIcons){Get-ChildItem -LiteralPath $taskIcons -Filter '*.png' | Remove-Item}
& (Join-Path $PSScriptRoot 'export-icons.ps1') -Destination $taskIcons
# The app logo is drawn at each size (scripts/draw-logo.ps1).
. (Join-Path $PSScriptRoot 'draw-logo.ps1')
Get-ChildItem -LiteralPath $taskAssets -Filter '*Logo*.png' | Remove-Item
Save-PackageLogos $taskAssets
$taskBitmap=[System.Drawing.Bitmap]::new(600,400)
$taskGraphics=[System.Drawing.Graphics]::FromImage($taskBitmap)
$taskGraphics.Clear([System.Drawing.ColorTranslator]::FromHtml('#24262F'))
$taskFont=[System.Drawing.Font]::new('Segoe UI',[single]19)
$taskBrush=[System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
$taskSmallFont=[System.Drawing.Font]::new('Segoe UI',[single]13)
$taskGraphics.DrawString("XSound Plus 2",$taskFont,$taskBrush,[single]28,[single]24)
$taskGraphics.DrawString("Battery 80%",$taskSmallFont,$taskBrush,[single]30,[single]76)
$taskIndex=0
foreach($taskPreset in 'XBass Off','XBass','MaxSound','Rock','Audiobook','Jazz'){
    $taskX=28+($taskIndex%3)*182;$taskY=120+[math]::Floor($taskIndex/3)*48;$taskIndex++
    $taskPanel=[System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#36383D'))
    $taskGraphics.FillRectangle($taskPanel,$taskX,$taskY,170,40);$taskPanel.Dispose()
    $taskGraphics.DrawString($taskPreset,$taskSmallFont,$taskBrush,[single]($taskX+12),[single]($taskY+8))
}
$taskGraphics.DrawString("Speaker settings   ›",$taskFont,$taskBrush,[single]30,[single]232)
$taskGraphics.DrawString("Voice prompts and auto shutdown",$taskSmallFont,$taskBrush,[single]30,[single]280)
$taskGraphics.DrawString("Illustrative preview",$taskSmallFont,$taskBrush,[single]30,[single]364)
$taskBitmap.Save((Join-Path $taskAssets 'Preview.png'),[System.Drawing.Imaging.ImageFormat]::Png)
$taskSmallFont.Dispose();$taskBrush.Dispose();$taskFont.Dispose();$taskGraphics.Dispose();$taskBitmap.Dispose()
Copy-Item -LiteralPath (Join-Path $taskAssets 'StoreLogo.png') -Destination (Join-Path $taskStage 'Public\Icon.png') -Force

$taskSdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\10.0.26100.0\x64'
# Index the scale and target-size logo variants; without resources.pri Windows ignores them
# and plates the taskbar icon in the accent colour.
$taskPri=Join-Path $taskRoot 'build\priconfig.xml'
& (Join-Path $taskSdk 'makepri.exe') createconfig /cf $taskPri /dq en-US /pv 10.0.0 /o | Out-Null
if($LASTEXITCODE -ne 0){throw 'makepri createconfig failed.'}
& (Join-Path $taskSdk 'makepri.exe') new /pr $taskStage /cf $taskPri /mn (Join-Path $taskStage 'AppxManifest.xml') /of (Join-Path $taskStage 'resources.pri') /o | Out-Null
if($LASTEXITCODE -ne 0){throw 'makepri new failed.'}

if($Register){
    # Developer Mode registers the staged folder in place; no certificate is created or trusted.
    Add-AppxPackage -Register (Join-Path $taskStage 'AppxManifest.xml') -ForceApplicationShutdown
    $taskInstalled=Get-AppxPackage -Name Kanu.TribitConnect
    if(-not $taskInstalled){throw 'Registration could not be confirmed.'}
    Write-Output "Registered Tribit Connect $($taskInstalled.Version) from $taskStage. Add XSound Plus 2 from Win + W > Add widgets."
    return
}
$taskCert=Get-ChildItem Cert:\CurrentUser\My | Where-Object {$_.Subject -eq 'CN=TribitConnect.Local' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(7)} | Sort-Object NotAfter -Descending | Select-Object -First 1
if(-not $taskCert){$taskCert=New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=TribitConnect.Local' -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(1)}
$taskOut=Join-Path $taskRoot 'out'
$taskPackage=Join-Path $taskOut "TribitConnect_$taskVersion.msix"
& (Join-Path $taskSdk 'makeappx.exe') pack /d $taskStage /p $taskPackage /o
if($LASTEXITCODE -ne 0){throw 'MSIX validation/packaging failed.'}
& (Join-Path $taskSdk 'signtool.exe') sign /fd SHA256 /sha1 $taskCert.Thumbprint /s My $taskPackage
if($LASTEXITCODE -ne 0){throw 'MSIX signing failed.'}
$taskCer=Join-Path $taskOut 'TribitConnect.Local.cer'
Export-Certificate -Cert $taskCert -FilePath $taskCer -Force | Out-Null
Write-Output "Signed package: $taskPackage"

# The one-file installer: TribitConnect-Setup.exe embeds the package and certificate (src/setup.cpp).
$taskSetup=Join-Path $taskRoot 'build\setup'
New-Item -Path $taskSetup -ItemType Directory -Force | Out-Null
Copy-Item -LiteralPath $taskPackage -Destination (Join-Path $taskSetup 'payload.msix') -Force
Copy-Item -LiteralPath $taskCer -Destination (Join-Path $taskSetup 'payload.cer') -Force
(Get-Item -LiteralPath (Join-Path $taskSetup 'setup.rc')).LastWriteTime=Get-Date   # the resource compiler does not track the payload
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$taskCmake=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake --build (Join-Path $taskRoot 'build') --config Release --target TribitConnectSetup
if($LASTEXITCODE -ne 0){throw 'Setup build failed.'}
$taskInstaller=Join-Path $taskOut 'TribitConnect-Setup.exe'
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\bin\Release\TribitConnect-Setup.exe') -Destination $taskInstaller -Force
& (Join-Path $taskSdk 'signtool.exe') sign /fd SHA256 /sha1 $taskCert.Thumbprint /s My $taskInstaller
if($LASTEXITCODE -ne 0){throw 'Setup signing failed.'}
Write-Output "Installer: $taskInstaller"
Write-Output 'The private signing key stays in the Windows certificate store; no PFX is exported.'
