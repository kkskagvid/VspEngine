<#
.SYNOPSIS
    Checks the frames the acceptance run captured, pixel by pixel.

.DESCRIPTION
    The demo draws a GREY cube standing on a ground plate, under a sky the
    Skybox shader paints, seen through a third-person camera that always keeps
    the cube centred, with a flat interface panel on top. Those facts are what
    the screenshots can prove, so they are what is checked here:

      1. the SKY is drawn - the band at the top of the frame, which the ground
         plate never reaches, is the sky's blue and not the near-black colour the
         frame used to be cleared to;
      2. the cube is ON SCREEN in every shot - the camera follows it, so it can
         never leave the frame, however far it moves;
      3. the cube is GREY - the pixels of the cube carry no hue at all, which is
         what tells a uniformly grey cube apart from the per-face coloured one
         the demo used to draw;
      4. the LIGHT comes from straight above - the top of the cube is far
         brighter than the sides, because only the top face catches the light and
         every face is the same colour;
      5. the INTERFACE is drawn on top - the panel of the flat theme appears in
         the top-left corner where it is pinned.

    Three screen regions keep the checks apart, and they are the regions the
    demo's own layout defines:

      * the top-left corner holds the HUD panel;
      * the middle of the frame holds the cube (the follow camera keeps it
        there);
      * the top of the frame, to the right of the panel, holds the sky (the
        ground plate ends below it).

    What the shots cannot show - which way the cube moves when a key is held,
    which way it spins after T, and that the pointer turns and tilts the camera -
    is read out of the engine log by RunAcceptance.ps1, because the camera keeps
    the cube centred and a moving object is therefore invisible in a still frame.
#>
param(
    # Directory holding Launch.exe and the shots written by the acceptance run.
    [string]$RunDirectory = "Engine\Intermediate\Binaries\Debug_x64"
)

Add-Type -AssemblyName System.Drawing

# The frame is scanned at 320x180 - one pixel per four of the 1280x720 capture -
# which is fine enough to separate the cube from the plate behind it and coarse
# enough to keep the scan quick.
$scanWidth = 320
$scanHeight = 180

# The regions above, in scanned pixels. The HUD panel is 330x214 device pixels at
# (18, 18), so the corner region below covers it with room to spare.
$interfaceRegionWidth = 110
$interfaceRegionHeight = 90
$cubeRegionLeft = 110
$cubeRegionRight = 214
$cubeRegionTop = 40
$cubeRegionBottom = 140
$skyRegionLeft = 120
$skyRegionBottom = 10

# What makes a pixel part of the cube: it carries no hue (a neutral grey, which
# neither the blue sky nor the cooler ground plate is) and it is not one step away
# from black.
$maximumCubeSaturation = 0.18
$minimumCubeChannel = 25

# What makes the sky BAND a sky rather than the colour the frame is cleared to:
# it is bright and it carries a colour. Both skies the demo shows pass this - the
# engine's daylight blue and the dusk the game replaces it with - while the clear
# colour (nearly black, and grey) fails it. Which sky it is belongs to the
# cross-shot check below, not to this one.
$minimumSkyLuminance = 60.0
$minimumSkySaturation = 0.20

# One analysed frame.
function Analyze-Shot([string]$path)
{
    $bitmap = [System.Drawing.Image]::FromFile($path)

    # Downscale for fast scanning.
    $small = New-Object System.Drawing.Bitmap($scanWidth, $scanHeight)
    $g = [System.Drawing.Graphics]::FromImage($small)
    $g.DrawImage($bitmap, 0, 0, $scanWidth, $scanHeight)
    $g.Dispose()
    $bitmap.Dispose()

    $cubePixelCount = 0
    $cubeSaturationSum = 0.0
    $cubeChannelSpreadSum = 0
    $cubeMinY = $scanHeight
    $cubeMaxY = -1
    $interfacePixelCount = 0
    $skyPixelCount = 0
    $skyRedSum = 0.0; $skyGreenSum = 0.0; $skyBlueSum = 0.0; $skyBandPixelCount = 0
    $skySaturationSum = 0.0; $skyLuminanceSum = 0.0

    # The cube's pixels are collected first, because the lighting check below
    # splits them by height and needs the band they occupy.
    $cubeLuminance = New-Object 'System.Collections.Generic.List[double]'
    $cubeY = New-Object 'System.Collections.Generic.List[int]'

    for ($y = 0; $y -lt $scanHeight; $y++)
    {
        for ($x = 0; $x -lt $scanWidth; $x++)
        {
            $pixel = $small.GetPixel($x, $y)
            $r = [int]$pixel.R; $gp = [int]$pixel.G; $b = [int]$pixel.B

            $maxChannel = [math]::Max($r, [math]::Max($gp, $b))
            $minChannel = [math]::Min($r, [math]::Min($gp, $b))

            # Saturation, computed without a division by zero on black.
            $saturation = 0.0
            if ($maxChannel -gt 0) { $saturation = ($maxChannel - $minChannel) / [double]$maxChannel }

            $isInterfaceRegion = ($x -lt $interfaceRegionWidth -and $y -lt $interfaceRegionHeight)
            $isSkyRegion = ($y -lt $skyRegionBottom -and $x -ge $skyRegionLeft)

            # ---- 1. The interface, in the corner its panel is pinned to ------
            # A flat surface clearly brighter than the background and clearly not
            # saturated, or the theme's saturated accent blue.
            if ($isInterfaceRegion)
            {
                $luminance = (0.2126 * $r) + (0.7152 * $gp) + (0.0722 * $b)
                if ($luminance -gt 25.0 -and $saturation -lt 0.30) { $interfacePixelCount++ }
                if ($b -gt 100 -and ($b - $r) -gt 60 -and $gp -gt $r) { $interfacePixelCount++ }
            }

            # ---- 2. The sky, in the band above the ground plate --------------
            if ($isSkyRegion)
            {
                $skyRedSum += $r; $skyGreenSum += $gp; $skyBlueSum += $b
                $skySaturationSum += $saturation
                $skyLuminanceSum += (0.2126 * $r) + (0.7152 * $gp) + (0.0722 * $b)
                $skyBandPixelCount++
                if ($b -gt 100 -and ($b - $r) -gt 25 -and $b -ge $gp) { $skyPixelCount++ }
            }

            # ---- 3. The cube, in the middle of the frame ---------------------
            if (-not $isInterfaceRegion -and
                $x -ge $cubeRegionLeft -and $x -lt $cubeRegionRight -and
                $y -ge $cubeRegionTop -and $y -lt $cubeRegionBottom -and
                $maxChannel -gt $minimumCubeChannel -and $saturation -lt $maximumCubeSaturation)
            {
                $cubePixelCount++
                $cubeSaturationSum += $saturation
                $cubeChannelSpreadSum += ($maxChannel - $minChannel)
                if ($y -lt $cubeMinY) { $cubeMinY = $y }
                if ($y -gt $cubeMaxY) { $cubeMaxY = $y }
                $cubeLuminance.Add((0.2126 * $r) + (0.7152 * $gp) + (0.0722 * $b))
                $cubeY.Add($y)
            }

        }
    }
    $small.Dispose()

    # ---- The lighting, over the cube's own pixels ----------------------------
    # The band the cube occupies is split in half: the top face is fully lit while
    # every other face receives the ambient fill only.
    $meanTopLuminance = 0.0
    $meanBottomLuminance = 0.0
    if ($cubePixelCount -gt 0)
    {
        $midY = [math]::Floor(($cubeMinY + $cubeMaxY) / 2.0)
        $topSum = 0.0; $topCount = 0
        $bottomSum = 0.0; $bottomCount = 0
        for ($index = 0; $index -lt $cubeLuminance.Count; $index++)
        {
            if ($cubeY[$index] -lt $midY) { $topSum += $cubeLuminance[$index]; $topCount++ }
            else { $bottomSum += $cubeLuminance[$index]; $bottomCount++ }
        }
        if ($topCount -gt 0) { $meanTopLuminance = $topSum / $topCount }
        if ($bottomCount -gt 0) { $meanBottomLuminance = $bottomSum / $bottomCount }
    }

    $meanCubeSaturation = 0.0
    $meanCubeChannelSpread = 0.0
    if ($cubePixelCount -gt 0)
    {
        $meanCubeSaturation = $cubeSaturationSum / $cubePixelCount
        $meanCubeChannelSpread = $cubeChannelSpreadSum / [double]$cubePixelCount
    }

    $meanSkyRed = 0.0; $meanSkyGreen = 0.0; $meanSkyBlue = 0.0
    $meanSkySaturation = 0.0; $meanSkyLuminance = 0.0
    if ($skyBandPixelCount -gt 0)
    {
        $meanSkyRed = $skyRedSum / $skyBandPixelCount
        $meanSkyGreen = $skyGreenSum / $skyBandPixelCount
        $meanSkyBlue = $skyBlueSum / $skyBandPixelCount
        $meanSkySaturation = $skySaturationSum / $skyBandPixelCount
        $meanSkyLuminance = $skyLuminanceSum / $skyBandPixelCount
    }

    [PSCustomObject]@{
        Name                  = [System.IO.Path]::GetFileName($path)
        CubePixels            = $cubePixelCount
        MeanCubeSaturation    = [math]::Round($meanCubeSaturation, 3)
        MeanCubeChannelSpread = [math]::Round($meanCubeChannelSpread, 1)
        MeanTopLuminance      = [math]::Round($meanTopLuminance, 1)
        MeanBottomLuminance   = [math]::Round($meanBottomLuminance, 1)
        InterfacePixels       = $interfacePixelCount
        SkyPixels             = $skyPixelCount
        SkyBandPixels         = $skyBandPixelCount
        MeanSkyRed            = [math]::Round($meanSkyRed, 1)
        MeanSkyGreen          = [math]::Round($meanSkyGreen, 1)
        MeanSkyBlue           = [math]::Round($meanSkyBlue, 1)
        MeanSkySaturation     = [math]::Round($meanSkySaturation, 3)
        MeanSkyLuminance      = [math]::Round($meanSkyLuminance, 1)
        SkyBlueLead           = [math]::Round($meanSkyBlue - $meanSkyRed, 1)
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
$checks = @()

foreach ($row in $rows)
{
    # 1. The sky is drawn. The band at the top of the frame is above the ground
    #    plate in every camera pose the demo uses, so it can only be the sky - the
    #    ENGINE's, painted before any pass of the game's - and it is bright and
    #    coloured, where the colour a frame is cleared to is nearly black and grey.
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the engine's sky is drawn (bright, coloured top band)"
        Pass = ($row.SkyBandPixels -gt 0 -and
                $row.MeanSkyLuminance -gt $minimumSkyLuminance -and
                $row.MeanSkySaturation -gt $minimumSkySaturation)
    }

    # 2. The cube is on screen in every single shot. The camera follows it, so
    #    this is the check that proves the follow camera works: a cube that ran
    #    away from a static camera would leave the frame.
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the cube is on screen"
        Pass = ($row.CubePixels -gt 300)
    }

    # 3. The cube is GREY: the pixels of it carry no hue worth speaking of. A
    #    per-face palette would show here as a saturation near 0.5 instead of 0.1 -
    #    and would fail "the cube is on screen" first, because that counts exactly
    #    these neutral pixels. Two checks, one measurement, which is why a cube
    #    that went back to being colourful cannot pass either of them.
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the cube is grey (saturation $($row.MeanCubeSaturation), channel spread $($row.MeanCubeChannelSpread))"
        Pass = ($row.MeanCubeSaturation -lt 0.15 -and $row.MeanCubeChannelSpread -lt 20.0)
    }

    # 4. The light comes from STRAIGHT ABOVE: the upper part of the cube is much
    #    brighter than the lower part, because only the top face catches the light
    #    - every face has the same colour now, so the shading is all there is.
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the top of the cube is brighter than the sides"
        Pass = ($row.MeanTopLuminance -gt ($row.MeanBottomLuminance * 1.3) -and $row.MeanTopLuminance -gt 60)
    }

    # 5. The interface is drawn on top of the scene: the top-left corner, where
    #    the HUD panel is pinned, holds a large flat surface the 3D scene does not
    #    put there.
    $checks += [PSCustomObject]@{
        Check = "$($row.Name): the interface panel is drawn"
        Pass = ($row.InterfacePixels -gt 400)
    }
}

# 6. REPLACING THE MATERIAL REPLACED THE SKY. The demo puts a material of its own
#    in RenderSettings.Skybox partway through the run, and the engine paints the
#    frame with whatever that setting names. Nothing else about the frame changes -
#    no pass is added, no shader is touched - so the sky band's COLOUR is the whole
#    evidence: it is the engine's blue before the swap and the game's dusk after it.
if ($rows.Count -ge 2) {
    $firstSky = $rows[0]
    $lastSky = $rows[$rows.Count - 1]
    $checks += [PSCustomObject]@{
        Check = ("the engine's default sky is blue (red-blue balance $($firstSky.SkyBlueLead))")
        Pass = ($firstSky.SkyBlueLead -gt 20.0)
    }
    $checks += [PSCustomObject]@{
        Check = ("the game's sky material replaced it (red-blue balance $($lastSky.SkyBlueLead))")
        Pass = ($lastSky.SkyBlueLead -lt -20.0)
    }
}

$checks | Format-Table -AutoSize

# @() matters: a pipeline that yields ONE failed check hands back the object
# itself, and .Count on a single object is $null rather than 1 - which would make
# one failure read as none.
$failed = @($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + $checks.Count + " checks passed")
if ($failed -gt 0) { exit 1 }
