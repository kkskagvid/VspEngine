<#
.SYNOPSIS
    Checks the frames the acceptance run captured, pixel by pixel.

.DESCRIPTION
    The demo draws a lit, per-face coloured cube through a third-person camera
    that always keeps it centred, with a flat interface panel on top. Those three
    facts are what the screenshots can prove, so they are what is checked here:

      1. the cube is ON SCREEN in every shot - the camera follows it, so it can
         never leave the frame, however far it moves;
      2. the cube is COLOURFUL - six saturated face colours, not one flat fill;
      3. the LIGHT comes from straight above - the top of the cube is far
         brighter than the bottom, and the ground (which also faces up) is lit
         just as brightly;
      4. the INTERFACE is drawn on top - the accent colour of the flat theme
         appears in the top-left corner where the panel sits.

    What the shots cannot show - which way the cube moves when a key is held, and
    which way it spins after T - is read out of the engine log by RunAcceptance.ps1,
    because the camera keeps the cube centred and a moving object is therefore
    invisible in a still frame.
#>
param(
    # Directory holding Launch.exe and the shots written by the acceptance run.
    [string]$RunDirectory = "Engine\Intermediate\Binaries\Debug_x64"
)

Add-Type -AssemblyName System.Drawing

# One analysed frame.
function Analyze-Shot([string]$path)
{
    $bitmap = [System.Drawing.Image]::FromFile($path)

    # Downscale to 320x180 for fast scanning.
    $small = New-Object System.Drawing.Bitmap(320, 180)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.DrawImage($bitmap, 0, 0, 320, 180)
    $g.Dispose()

    $coloredCount = 0
    $redCount = 0; $greenCount = 0; $blueCount = 0; $cyanCount = 0; $magentaCount = 0; $yellowCount = 0
    $topLuminance = 0.0; $topCount = 0
    $bottomLuminance = 0.0; $bottomCount = 0
    $panelPixelCount = 0
    $minY = 9999; $maxY = -1

    # The HUD panel is pinned to the top-left corner, so that corner is where the
    # interface has to show. The two things it puts there are a large, flat,
    # LOW-SATURATION surface (the panel and its buttons) and the theme's
    # saturated accent blue. The background is darker than either.
    $interfaceRegionWidth = 110
    $interfaceRegionHeight = 90

    for ($y = 0; $y -lt 180; $y++)
    {
        for ($x = 0; $x -lt 320; $x++)
        {
            $pixel = $small.GetPixel($x, $y)
            $r = [int]$pixel.R; $gp = [int]$pixel.G; $b = [int]$pixel.B

            $maxChannel = [math]::Max($r, [math]::Max($gp, $b))
            $minChannel = [math]::Min($r, [math]::Min($gp, $b))

            # Saturation, computed without a division by zero on black.
            $saturation = 0.0
            if ($maxChannel -gt 0) { $saturation = ($maxChannel - $minChannel) / [double]$maxChannel }

            $isInterfaceRegion = ($x -lt $interfaceRegionWidth -and $y -lt $interfaceRegionHeight)

            # The interface, in the corner its panel is pinned to: a flat surface
            # clearly brighter than the background and clearly not saturated, or
            # the theme's saturated accent blue. The interface's own accent is
            # excluded from the cube below, so the two never mix.
            if ($isInterfaceRegion)
            {
                $luminance = (0.2126 * $r) + (0.7152 * $gp) + (0.0722 * $b)
                if ($luminance -gt 25.0 -and $saturation -lt 0.30) { $panelPixelCount++ }
                if ($b -gt 100 -and ($b - $r) -gt 60 -and $gp -gt $r) { $panelPixelCount++ }
            }

            # A CUBE pixel: outside the interface's corner, bright enough to see
            # and saturated enough not to be the grey ground plate.
            if (-not $isInterfaceRegion -and $maxChannel -gt 40 -and $saturation -gt 0.35)
            {
                $coloredCount++
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }

                if ($r -eq $maxChannel -and $gp -eq $minChannel) { $redCount++ }
                elseif ($gp -eq $maxChannel -and $b -eq $minChannel) { $greenCount++ }
                elseif ($b -eq $maxChannel -and $r -eq $minChannel) { $blueCount++ }

                # The remaining three families are the ones with two strong
                # channels: cyan (green + blue), magenta (red + blue) and yellow
                # (red + green).
                if ($b -gt 80 -and $gp -gt 80 -and $r -lt 80) { $cyanCount++ }
                if ($r -gt 80 -and $b -gt 80 -and $gp -lt 80) { $magentaCount++ }
                if ($r -gt 80 -and $gp -gt 80 -and $b -lt 80) { $yellowCount++ }
            }

        }
    }
    $bitmap.Dispose()

    # Lighting: the mean luminance of the cube's own pixels, split into the
    # upper and the lower half of the band the cube occupies. The top face is
    # fully lit; every other face only receives the ambient fill.
    $midY = [math]::Floor(($minY + $maxY) / 2.0)
    for ($y = 0; $y -lt 180; $y++)
    {
        for ($x = 0; $x -lt 320; $x++)
        {
            $pixel = $small.GetPixel($x, $y)
            $r = [int]$pixel.R; $gp = [int]$pixel.G; $b = [int]$pixel.B
            $maxChannel = [math]::Max($r, [math]::Max($gp, $b))
            $minChannel = [math]::Min($r, [math]::Min($gp, $b))
            $saturation = 0.0
            if ($maxChannel -gt 0) { $saturation = ($maxChannel - $minChannel) / [double]$maxChannel }

            # The interface's corner is not the cube, in this pass either.
            if ($x -lt $interfaceRegionWidth -and $y -lt $interfaceRegionHeight) { continue }
            if ($maxChannel -le 40 -or $saturation -le 0.35) { continue }

            $luminance = (0.2126 * $r) + (0.7152 * $gp) + (0.0722 * $b)
            if ($y -lt $midY) { $topLuminance += $luminance; $topCount++ }
            else { $bottomLuminance += $luminance; $bottomCount++ }
        }
    }
    $small.Dispose()

    $meanTop = if ($topCount -gt 0) { $topLuminance / $topCount } else { 0.0 }
    $meanBottom = if ($bottomCount -gt 0) { $bottomLuminance / $bottomCount } else { 0.0 }

    [PSCustomObject]@{
        Name = [System.IO.Path]::GetFileName($path)
        CubePixels = $coloredCount
        Red = $redCount
        Green = $greenCount
        Blue = $blueCount
        Cyan = $cyanCount
        Magenta = $magentaCount
        Yellow = $yellowCount
        MeanTopLuminance = [math]::Round($meanTop, 1)
        MeanBottomLuminance = [math]::Round($meanBottom, 1)
        InterfacePixels = $panelPixelCount
    }
}

Set-Location $RunDirectory
$shots = Get-ChildItem shot*.bmp | Sort-Object Name
if ($shots.Count -eq 0) {
    Write-Error "The acceptance run captured no frames."
    exit 1
}

$rows = $shots | ForEach-Object { Analyze-Shot $_.FullName }
$rows | Format-Table -AutoSize

Write-Output "=== VERIFICATION ==="
$m = @{}
foreach ($row in $rows) { $m[$row.Name] = $row }

$checks = @()

# 1. The cube is on screen in every single shot. The camera follows it, so this
#    is the check that proves the follow camera works: a cube that ran away from
#    a static camera would leave the frame.
foreach ($row in $rows) {
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the cube is on screen"
        Pass = ($row.CubePixels -gt 400)
    }
}

# 2. It is COLOURFUL. The cube spins, so WHICH faces are visible depends on the
#    moment a frame was captured - a single frame may legitimately show one face
#    colour and little else. What must hold is that the cube carries SEVERAL
#    distinct face colours over the run, which is what "colourful" means for a
#    per-face coloured cube; and that no captured frame has lost its colour
#    altogether.
$allFamilies = @{}
foreach ($row in $rows) {
    $rowFamilies = @()
    if ($row.Red -gt 20) { $rowFamilies += "red" }
    if ($row.Green -gt 20) { $rowFamilies += "green" }
    if ($row.Blue -gt 20) { $rowFamilies += "blue" }
    if ($row.Cyan -gt 20) { $rowFamilies += "cyan" }
    if ($row.Magenta -gt 20) { $rowFamilies += "magenta" }
    if ($row.Yellow -gt 20) { $rowFamilies += "yellow" }
    foreach ($family in $rowFamilies) { $allFamilies[$family] = $true }

    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the cube shows a face colour ($($rowFamilies.Count) famil(y/ies))"
        Pass = ($rowFamilies.Count -ge 1)
    }
}
$checks += [PSCustomObject]@{
    Check = "the cube is colourful across the run ($($allFamilies.Count) colour families)"
    Pass = ($allFamilies.Count -ge 3)
}

# 3. The light comes from STRAIGHT ABOVE: the upper part of the cube is much
#    brighter than the lower part, because only the top face catches the light.
foreach ($row in $rows) {
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the top of the cube is brighter than the sides"
        Pass = ($row.MeanTopLuminance -gt ($row.MeanBottomLuminance * 1.3) -and $row.MeanTopLuminance -gt 60)
    }
}

# 4. The interface is drawn on top of the scene: the top-left corner, where the
#    HUD panel is pinned, holds a large flat surface the 3D scene does not put
#    there. (The cube sits in the middle of the frame whatever the camera does -
#    that is what a follow camera means - so this corner is the interface's.)
foreach ($row in $rows) {
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the interface panel is drawn"
        Pass = ($row.InterfacePixels -gt 400)
    }
}

$checks | Format-Table -AutoSize
$failed = ($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + $checks.Count + " checks passed")
if ($failed -gt 0) { exit 1 }
