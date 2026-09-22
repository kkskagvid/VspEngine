param(
    # Directory holding Launch.exe and the shots written by the acceptance run.
    [string]$RunDirectory = "Engine\Intermediate\Binaries\Debug_x64"
)

Add-Type -AssemblyName System.Drawing


function Analyze-Shot([string]$path)
{
    $bitmap = [System.Drawing.Image]::FromFile($path)

    # Downscale to 320x180 for fast scanning.
    $small = New-Object System.Drawing.Bitmap(320, 180)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.DrawImage($bitmap, 0, 0, 320, 180)
    $g.Dispose()

    $minX = 9999; $minY = 9999; $maxX = -1; $maxY = -1
    $redCount = 0; $greenCount = 0; $blueCount = 0; $total = 0

    for ($y = 0; $y -lt 180; $y++)
    {
        for ($x = 0; $x -lt 320; $x++)
        {
            $pixel = $small.GetPixel($x, $y)
            $r = $pixel.R; $g2 = $pixel.G; $b = $pixel.B

            # Non-background pixel (background is dark blue-grey ~ (15,15,26)).
            if (($r + $g2 + $b) -gt 120)
            {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
                $total++
            }

            if ($r -gt 150 -and $g2 -lt 60 -and $b -lt 60) { $redCount++ }
            if ($g2 -gt 150 -and $r -lt 60 -and $b -lt 60) { $greenCount++ }
            if ($b -gt 150 -and $r -lt 60 -and $g2 -lt 60) { $blueCount++ }
        }
    }
    $bitmap.Dispose()
    $small.Dispose()

    $cx = if ($total -gt 0) { [math]::Round(($minX + $maxX) / 2.0, 1) } else { -1 }
    $cy = if ($total -gt 0) { [math]::Round(($minY + $maxY) / 2.0, 1) } else { -1 }

    [PSCustomObject]@{
        Name = [System.IO.Path]::GetFileName($path)
        CentroidX = $cx
        CentroidY = $cy
        PixelCount = $total
        Red = $redCount
        Green = $greenCount
        Blue = $blueCount
    }
}

Set-Location $RunDirectory
$shots = Get-ChildItem shot*.bmp | Sort-Object Name
$rows = $shots | ForEach-Object { Analyze-Shot $_.FullName }
$rows | Format-Table -AutoSize

Write-Output "=== VERIFICATION ==="
$m = @{}
foreach ($row in $rows) { $m[$row.Name] = $row }

$checks = @()

# 1. D moves right
$checks += [PSCustomObject]@{ Check = "D moves triangle right"; Pass = ($m["shot1_after_D.bmp"].CentroidX -gt $m["shot0_initial.bmp"].CentroidX + 5) }
# 2. W moves up (smaller Y)
$checks += [PSCustomObject]@{ Check = "W moves triangle up"; Pass = ($m["shot2_after_W.bmp"].CentroidY -lt $m["shot1_after_D.bmp"].CentroidY - 5) }
# 3. A moves left
$checks += [PSCustomObject]@{ Check = "A moves triangle left"; Pass = ($m["shot3_after_A.bmp"].CentroidX -lt $m["shot2_after_W.bmp"].CentroidX - 5) }
# 4. S moves down (larger Y)
$checks += [PSCustomObject]@{ Check = "S moves triangle down"; Pass = ($m["shot4_after_S.bmp"].CentroidY -gt $m["shot3_after_A.bmp"].CentroidY + 5) }
# 5. R resets to center (close to initial centroid)
$dx = [math]::Abs($m["shot5_after_R.bmp"].CentroidX - $m["shot0_initial.bmp"].CentroidX)
$dy = [math]::Abs($m["shot5_after_R.bmp"].CentroidY - $m["shot0_initial.bmp"].CentroidY)
$checks += [PSCustomObject]@{ Check = "R resets to start position"; Pass = ($dx -lt 6 -and $dy -lt 6) }
# 6. T cycles: red -> blue -> green -> multicolor
$checks += [PSCustomObject]@{ Check = "T1 = single red"; Pass = ($m["shot6_T_red.bmp"].Red -gt 50 -and $m["shot6_T_red.bmp"].Green -lt 15 -and $m["shot6_T_red.bmp"].Blue -lt 15) }
$checks += [PSCustomObject]@{ Check = "T2 = single blue"; Pass = ($m["shot7_T_blue.bmp"].Blue -gt 50 -and $m["shot7_T_blue.bmp"].Red -lt 15 -and $m["shot7_T_blue.bmp"].Green -lt 15) }
$checks += [PSCustomObject]@{ Check = "T3 = single green"; Pass = ($m["shot8_T_green.bmp"].Green -gt 50 -and $m["shot8_T_green.bmp"].Red -lt 15 -and $m["shot8_T_green.bmp"].Blue -lt 15) }
$checks += [PSCustomObject]@{ Check = "T4 = multicolor"; Pass = ($m["shot9_T_multi.bmp"].Red -gt 20 -and $m["shot9_T_multi.bmp"].Green -gt 20 -and $m["shot9_T_multi.bmp"].Blue -gt 20) }
$checks += [PSCustomObject]@{ Check = "initial = multicolor"; Pass = ($m["shot0_initial.bmp"].Red -gt 20 -and $m["shot0_initial.bmp"].Green -gt 20 -and $m["shot0_initial.bmp"].Blue -gt 20) }

# 7. The depth test: sent behind the cube the triangle disappears, and brought
#    back it is exactly where it was. Without a depth buffer the cube could not
#    hide anything - the triangle is drawn AFTER it.
$checks += [PSCustomObject]@{ Check = "Z hides the triangle behind the cube"; Pass = ($m["shot10_behind_cube.bmp"].PixelCount -lt 400 -and $m["shot10_behind_cube.bmp"].PixelCount -lt ($m["shot0_initial.bmp"].PixelCount / 4)) }
$checks += [PSCustomObject]@{ Check = "X brings it back in front"; Pass = ($m["shot11_back_in_front.bmp"].PixelCount -gt ($m["shot0_initial.bmp"].PixelCount * 0.9)) }

# 8. The same scene through three projections: an orthographic camera keeps a
#    world unit the same size, a 45-degree perspective camera makes the same
#    triangle smaller, and a 50 mm lens on a full-frame sensor is close to it
#    again. The three images therefore cannot all be identical.
$orthographicPixels = $m["shot14_camera_orthographic.bmp"].PixelCount
$perspectivePixels = $m["shot12_camera_perspective.bmp"].PixelCount
$physicalPixels = $m["shot13_camera_physical.bmp"].PixelCount
$checks += [PSCustomObject]@{ Check = "the projection changes what the same scene looks like"; Pass = ($perspectivePixels -ne $orthographicPixels -and $physicalPixels -ne $perspectivePixels) }
# A 45-degree perspective camera sees about five world units of height where the
# orthographic one sees three, so the same triangle covers fewer pixels; the
# 50 mm lens on a full-frame sensor is narrower than both, so it covers more than
# the perspective camera again.
$checks += [PSCustomObject]@{ Check = "the perspective camera shows less of the triangle than the orthographic one"; Pass = ($perspectivePixels -gt 50 -and $perspectivePixels -lt $orthographicPixels) }
$checks += [PSCustomObject]@{ Check = "the 50 mm lens frames tighter than the 45-degree perspective camera"; Pass = ($physicalPixels -gt $perspectivePixels) }

$checks | Format-Table -AutoSize
$failed = ($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + $checks.Count + " checks passed")
if ($failed -gt 0) { exit 1 }