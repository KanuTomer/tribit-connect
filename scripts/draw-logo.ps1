# The app's logo: the XSound Plus 2 seen from the front, its EQ button lit orange and an
# equalizer curve drawn across the grille in the Sound Lab's LCD green. Drawn in a 100-unit
# square on a transparent background, so every size is rendered directly rather than scaled
# from one bitmap; small sizes get heavier strokes and lose the halo.
Add-Type -AssemblyName System.Drawing
function New-Stadium([single]$X,[single]$Y,[single]$W,[single]$H){
    $path=[System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($X,$Y,$H,$H,90,180);$path.AddArc(($X+$W-$H),$Y,$H,$H,270,180);$path.CloseFigure();$path
}
function Save-Logo([int]$Size,[string]$Path){
    $bitmap=[System.Drawing.Bitmap]::new($Size,$Size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g=[System.Drawing.Graphics]::FromImage($bitmap)
    try{
        $g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.PixelOffsetMode=[System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.ScaleTransform($Size/100,$Size/100)
        $small=$Size -lt 32
        $color={param($hex)[System.Drawing.ColorTranslator]::FromHtml($hex)}
        $brush={param($hex)[System.Drawing.SolidBrush]::new((& $color $hex))}
        # Top panel with a lit upper edge (it keeps the shape readable on a dark taskbar).
        $top=New-Stadium 4 17 92 54
        $g.FillPath((& $brush '#5a5b63'),$top)
        $rim=[System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(150,170,172,182),[single]$(if($small){2.4}else{1.4}))
        $g.DrawPath($rim,$top)
        # The body in front of it, then the recessed grille.
        $body=New-Stadium 4 27 92 54
        $shade=[System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.PointF]::new(0,27),[System.Drawing.PointF]::new(0,81),(& $color '#37383e'),(& $color '#1a1b1f'))
        $g.FillPath($shade,$body)
        $grille=New-Stadium 11 33.5 78 41;$g.FillPath((& $brush '#111215'),$grille)
        # The curve: a bass lift settling into a treble rise, with a faint LCD glow.
        $width=if($small){7}else{4.6}
        foreach($pass in @(@{w=$width*2.0;c=[System.Drawing.Color]::FromArgb(30,207,233,162)},@{w=$width;c=(& $color '#d2eca4')})){
            if($small -and $pass.w -gt $width){continue}
            $pen=[System.Drawing.Pen]::new($pass.c,[single]$pass.w)
            $pen.StartCap=$pen.EndCap=[System.Drawing.Drawing2D.LineCap]::Round;$pen.LineJoin=[System.Drawing.Drawing2D.LineJoin]::Round
            $curve=[System.Drawing.Drawing2D.GraphicsPath]::new()
            $curve.AddBezier([single]19,[single]61,[single]28,[single]40,[single]38,[single]42,[single]50,[single]56)
            $curve.AddBezier([single]50,[single]56,[single]61,[single]68,[single]72,[single]59,[single]81,[single]48)
            $g.DrawPath($pen,$curve);$pen.Dispose();$curve.Dispose()
        }
        # The EQ button's light on the top panel.
        $r=if($small){6.5}else{5}
        if(-not $small){$g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(80,255,90,20)),[single](67-9),[single](22-9),[single]18,[single]18)}
        $g.FillEllipse((& $brush '#ff5a14'),[single](67-$r),[single](22-$r),[single](2*$r),[single](2*$r))
        $g.FillEllipse([System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(140,255,214,180)),[single](67-$r*.45),[single](22-$r*.6),[single]($r*.7),[single]($r*.55))
        $bitmap.Save($Path,[System.Drawing.Imaging.ImageFormat]::Png)
    }finally{$g.Dispose();$bitmap.Dispose()}
}
# Every logo the package needs. Windows only draws the taskbar and Start icons without a
# coloured plate when "unplated" target-size variants exist, indexed by resources.pri.
function Save-PackageLogos([string]$Assets){
    foreach($t in 16,20,24,30,32,36,40,48,60,64,72,80,96,256){
        Save-Logo $t (Join-Path $Assets "Logo44.targetsize-$t.png")
        Save-Logo $t (Join-Path $Assets "Logo44.targetsize-${t}_altform-unplated.png")
        Save-Logo $t (Join-Path $Assets "Logo44.targetsize-${t}_altform-lightunplated.png")
    }
    foreach($s in @(@(100,1),@(125,1.25),@(150,1.5),@(200,2),@(400,4))){
        Save-Logo ([int](44*$s[1])) (Join-Path $Assets "Logo44.scale-$($s[0]).png")
        Save-Logo ([int](150*$s[1])) (Join-Path $Assets "Logo150.scale-$($s[0]).png")
    }
    # Unqualified copy for the widget picker and Public\Icon.png, which read the file directly.
    Save-Logo 50 (Join-Path $Assets 'StoreLogo.png')
}
# The executable's own icon (window title, taskbar previews, Alt+Tab): one .ico holding the logo
# drawn at each size. Small frames are 32-bit bitmaps, which every icon reader accepts; the
# 256 px frame is PNG, as Windows expects.
function Get-IcoFrame([int]$Size){
    $file=[IO.Path]::GetTempFileName();Save-Logo $Size $file
    try{
        if($Size -ge 256){return ,[IO.File]::ReadAllBytes($file)}
        $bmp=[System.Drawing.Bitmap]::FromFile($file)
        try{
            $data=$bmp.LockBits([System.Drawing.Rectangle]::new(0,0,$Size,$Size),'ReadOnly',[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $pixels=[byte[]]::new($data.Stride*$Size);[Runtime.InteropServices.Marshal]::Copy($data.Scan0,$pixels,0,$pixels.Length);$bmp.UnlockBits($data)
            $mask=[int]([math]::Floor(($Size+31)/32)*4)
            $ms=[IO.MemoryStream]::new();$w=[IO.BinaryWriter]::new($ms)
            # BITMAPINFOHEADER: the height counts the colour rows and the (empty) transparency mask.
            $w.Write([uint32]40);$w.Write([int32]$Size);$w.Write([int32](2*$Size));$w.Write([uint16]1);$w.Write([uint16]32)
            $w.Write([uint32]0);$w.Write([uint32]($Size*$Size*4+$mask*$Size));$w.Write([int32]0);$w.Write([int32]0);$w.Write([uint32]0);$w.Write([uint32]0)
            for($y=$Size-1;$y -ge 0;$y--){$w.Write($pixels,$y*$data.Stride,$Size*4)}   # rows bottom-up
            $w.Write([byte[]]::new($mask*$Size))
            $w.Flush();return ,$ms.ToArray()
        }finally{$bmp.Dispose()}
    }finally{Remove-Item $file}
}
function Save-Ico([string]$Path){
    $sizes=16,20,24,32,40,48,64,256;$frames=@(foreach($s in $sizes){,(Get-IcoFrame $s)})
    $ms=[IO.MemoryStream]::new();$w=[IO.BinaryWriter]::new($ms)
    $w.Write([uint16]0);$w.Write([uint16]1);$w.Write([uint16]$sizes.Count)
    $offset=6+16*$sizes.Count
    for($i=0;$i -lt $sizes.Count;$i++){$s=$sizes[$i]%256
        $w.Write([byte]$s);$w.Write([byte]$s);$w.Write([byte]0);$w.Write([byte]0);$w.Write([uint16]1);$w.Write([uint16]32)
        $w.Write([uint32]$frames[$i].Length);$w.Write([uint32]$offset);$offset+=$frames[$i].Length}
    foreach($f in $frames){$w.Write([byte[]]$f)}
    $w.Flush();[IO.File]::WriteAllBytes($Path,$ms.ToArray());$w.Dispose()
}
