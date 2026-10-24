# SVG path geometry used by the approved Lucide sources. Convert arcs to cubic
# segments for GDI+; keep the original SVG paths unchanged on disk.
function Convert-SvgPath([string]$Data) {
    $path=[Drawing.Drawing2D.GraphicsPath]::new()
    $tokens=@([regex]::Matches($Data,'[AaCcHhLlMmVvZz]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[Ee][-+]?\d+)?') | ForEach-Object {$_.Value})
    $i=0;$command='';$px=0.0;$py=0.0;$startX=0.0;$startY=0.0
    try {
        while($i -lt $tokens.Count){
            if($tokens[$i] -cmatch '^[A-Za-z]$'){
                $command=$tokens[$i];$i++
                if($command -in @('Z','z')){$path.CloseFigure();$px=$startX;$py=$startY;continue}
            }
            $relative=$command -cmatch '^[a-z]$';$op=$command.ToUpperInvariant()
            $count=switch($op){'M'{2};'L'{2};'H'{1};'V'{1};'C'{6};'A'{7};default{throw "Unsupported SVG command: $command"}}
            if($i+$count -gt $tokens.Count){throw 'Incomplete SVG path.'}
            $v=@(for($n=0;$n -lt $count;$n++){[double]::Parse($tokens[$i+$n],[Globalization.CultureInfo]::InvariantCulture)});$i+=$count
            switch($op){
                'M' {if($relative){$px+=$v[0];$py+=$v[1]}else{$px=$v[0];$py=$v[1]};$path.StartFigure();$startX=$px;$startY=$py;$command=if($relative){'l'}else{'L'}}
                'L' {$nx=$v[0];$ny=$v[1];if($relative){$nx+=$px;$ny+=$py};$path.AddLine([single]$px,[single]$py,[single]$nx,[single]$ny);$px=$nx;$py=$ny}
                'H' {$nx=$v[0];if($relative){$nx+=$px};$path.AddLine([single]$px,[single]$py,[single]$nx,[single]$py);$px=$nx}
                'V' {$ny=$v[0];if($relative){$ny+=$py};$path.AddLine([single]$px,[single]$py,[single]$px,[single]$ny);$py=$ny}
                'C' {if($relative){for($n=0;$n -lt 6;$n+=2){$v[$n]+=$px;$v[$n+1]+=$py}};$path.AddBezier([single]$px,[single]$py,[single]$v[0],[single]$v[1],[single]$v[2],[single]$v[3],[single]$v[4],[single]$v[5]);$px=$v[4];$py=$v[5]}
                'A' {
                    $rx=[Math]::Abs($v[0]);$ry=[Math]::Abs($v[1]);$phi=$v[2]*[Math]::PI/180;$large=$v[3] -ne 0;$sweep=$v[4] -ne 0;$nx=$v[5];$ny=$v[6]
                    if($relative){$nx+=$px;$ny+=$py}
                    if($rx -eq 0 -or $ry -eq 0){$path.AddLine([single]$px,[single]$py,[single]$nx,[single]$ny)}
                    elseif([Math]::Abs($px-$nx)+[Math]::Abs($py-$ny) -gt 0.000001){
                        $co=[Math]::Cos($phi);$si=[Math]::Sin($phi);$xp=$co*($px-$nx)/2+$si*($py-$ny)/2;$yp=-$si*($px-$nx)/2+$co*($py-$ny)/2
                        $lambda=$xp*$xp/($rx*$rx)+$yp*$yp/($ry*$ry);if($lambda -gt 1){$rx*=[Math]::Sqrt($lambda);$ry*=[Math]::Sqrt($lambda)}
                        $den=$rx*$rx*$yp*$yp+$ry*$ry*$xp*$xp
                        $factor=[Math]::Sqrt([Math]::Max(0.0,($rx*$rx*$ry*$ry-$den)/$den));if($large -eq $sweep){$factor=-$factor}
                        $cxp=$factor*$rx*$yp/$ry;$cyp=-$factor*$ry*$xp/$rx
                        $cx=$co*$cxp-$si*$cyp+($px+$nx)/2;$cy=$si*$cxp+$co*$cyp+($py+$ny)/2
                        $ux=($xp-$cxp)/$rx;$uy=($yp-$cyp)/$ry;$vx=(-$xp-$cxp)/$rx;$vy=(-$yp-$cyp)/$ry
                        $angle=[Math]::Atan2($uy,$ux);$delta=[Math]::Atan2($ux*$vy-$uy*$vx,$ux*$vx+$uy*$vy)
                        if(!$sweep -and $delta -gt 0){$delta-=2*[Math]::PI};if($sweep -and $delta -lt 0){$delta+=2*[Math]::PI}
                        $segments=[int][Math]::Ceiling([Math]::Abs($delta)/([Math]::PI/2));$step=$delta/$segments
                        for($segment=0;$segment -lt $segments;$segment++){
                            $end=$angle+$step;$k=4.0/3*[Math]::Tan($step/4)
                            $c0=[Math]::Cos($angle);$s0=[Math]::Sin($angle);$c1=[Math]::Cos($end);$s1=[Math]::Sin($end)
                            $x0=$cx+$co*$rx*$c0-$si*$ry*$s0;$y0=$cy+$si*$rx*$c0+$co*$ry*$s0
                            $x3=$cx+$co*$rx*$c1-$si*$ry*$s1;$y3=$cy+$si*$rx*$c1+$co*$ry*$s1
                            $dx0=-$co*$rx*$s0-$si*$ry*$c0;$dy0=-$si*$rx*$s0+$co*$ry*$c0
                            $dx1=-$co*$rx*$s1-$si*$ry*$c1;$dy1=-$si*$rx*$s1+$co*$ry*$c1
                            $path.AddBezier([single]$x0,[single]$y0,[single]($x0+$k*$dx0),[single]($y0+$k*$dy0),[single]($x3-$k*$dx1),[single]($y3-$k*$dy1),[single]$x3,[single]$y3);$angle=$end
                        }
                    }
                    $px=$nx;$py=$ny
                }
            }
        }
        return $path
    }catch{$path.Dispose();throw}
}

