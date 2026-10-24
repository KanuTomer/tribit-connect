param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'Convert-SvgPath.ps1')
$taskRoot=Split-Path $PSScriptRoot -Parent
New-Item -Path $Destination -ItemType Directory -Force | Out-Null
# Original SVG geometry is the source of truth; export only its small shape
# subset, without downloading a renderer or changing the editable artwork.
$taskThemes=@{light='#616161';dark='#C2C3C4';'hc-light'='#000000';'hc-dark'='#FFFFFF';accent='#4CC2FF';'accent-light'='#0067C0'}
foreach($taskSource in Get-ChildItem -LiteralPath (Join-Path $taskRoot 'assets\icons') -Filter '*.svg'){
    $taskSvg=[xml](Get-Content -LiteralPath $taskSource.FullName -Raw)
    foreach($taskTheme in $taskThemes.Keys){
        $taskBitmap=[System.Drawing.Bitmap]::new(96,96)
        $taskGraphics=[System.Drawing.Graphics]::FromImage($taskBitmap)
        $taskPen=[System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml($taskThemes[$taskTheme]),[single]2)
        try{
            $taskGraphics.Clear([System.Drawing.Color]::Transparent)
            $taskGraphics.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $taskGraphics.ScaleTransform(4,4)
            $taskPen.StartCap=$taskPen.EndCap=[System.Drawing.Drawing2D.LineCap]::Round
            $taskPen.LineJoin=[System.Drawing.Drawing2D.LineJoin]::Round
            foreach($taskElement in $taskSvg.DocumentElement.ChildNodes){
                $taskPen.Width=if($taskElement.'stroke-width'){[single]::Parse($taskElement.'stroke-width',[Globalization.CultureInfo]::InvariantCulture)}elseif($taskSvg.DocumentElement.'stroke-width'){[single]::Parse($taskSvg.DocumentElement.'stroke-width',[Globalization.CultureInfo]::InvariantCulture)}else{[single]2}
                if($taskElement.LocalName -eq 'ellipse'){
                    $taskCx=[single]$taskElement.cx;$taskCy=[single]$taskElement.cy;$taskRx=[single]$taskElement.rx;$taskRy=[single]$taskElement.ry
                    $taskGraphics.DrawEllipse($taskPen,[single]($taskCx-$taskRx),[single]($taskCy-$taskRy),[single](2*$taskRx),[single](2*$taskRy))
                }elseif($taskElement.LocalName -eq 'circle'){
                    $taskR=[single]$taskElement.r;$taskGraphics.DrawEllipse($taskPen,[single]([single]$taskElement.cx-$taskR),[single]([single]$taskElement.cy-$taskR),[single](2*$taskR),[single](2*$taskR))
                }elseif($taskElement.LocalName -eq 'line'){
                    $taskGraphics.DrawLine($taskPen,[single]$taskElement.x1,[single]$taskElement.y1,[single]$taskElement.x2,[single]$taskElement.y2)
                }elseif($taskElement.LocalName -eq 'polyline'){
                    [System.Drawing.PointF[]]$taskPoints=@(($taskElement.points -split '\s+') | ForEach-Object {$taskPair=$_ -split ',';[System.Drawing.PointF]::new([single]$taskPair[0],[single]$taskPair[1])})
                    $taskGraphics.DrawLines($taskPen,$taskPoints)
                }elseif($taskElement.LocalName -eq 'path'){
                    $taskPath=Convert-SvgPath $taskElement.d
                    try{
                        if($taskElement.fill -eq 'currentColor'){
                            $taskAlpha=if($taskElement.'fill-opacity'){[int](255*[double]::Parse($taskElement.'fill-opacity',[Globalization.CultureInfo]::InvariantCulture))}else{255}
                            $taskBrush=[System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($taskAlpha,$taskPen.Color));try{$taskGraphics.FillPath($taskBrush,$taskPath)}finally{$taskBrush.Dispose()}
                        }
                        $taskGraphics.DrawPath($taskPen,$taskPath)
                    }finally{$taskPath.Dispose()}
                }elseif($taskElement.LocalName -eq 'rect'){
                    $taskPath=[System.Drawing.Drawing2D.GraphicsPath]::new()
                    try{
                        $taskX=[single]$taskElement.x;$taskY=[single]$taskElement.y;$taskW=[single]$taskElement.width;$taskH=[single]$taskElement.height;$taskD=[single](2*[single]$taskElement.rx)
                        $taskPath.AddArc($taskX,$taskY,$taskD,$taskD,180,90)
                        $taskPath.AddArc(($taskX+$taskW-$taskD),$taskY,$taskD,$taskD,270,90)
                        $taskPath.AddArc(($taskX+$taskW-$taskD),($taskY+$taskH-$taskD),$taskD,$taskD,0,90)
                        $taskPath.AddArc($taskX,($taskY+$taskH-$taskD),$taskD,$taskD,90,90)
                        $taskPath.CloseFigure();$taskGraphics.DrawPath($taskPen,$taskPath)
                    }finally{$taskPath.Dispose()}
                }else{throw "Unsupported SVG element: $($taskElement.LocalName)"}
            }
            $taskBitmap.Save((Join-Path $Destination "$($taskSource.BaseName)-$taskTheme.png"),[System.Drawing.Imaging.ImageFormat]::Png)
        }finally{$taskPen.Dispose();$taskGraphics.Dispose();$taskBitmap.Dispose()}
    }
}
