<#
.SYNOPSIS
    Runs the engine's acceptance test end to end and reports the result.

.DESCRIPTION
    Builds the solution (unless -SkipBuild is given), runs the demo with a
    deterministic frame clock (-fixed-delta-time), captures the frames the
    checks analyse, and fails when the engine reported an error or wrote a
    warning into its log.

    The fixed frame clock is what makes the run reproducible: the engine
    advances its clock by exactly one step per frame instead of reading the wall
    clock, so frame N always happens at N * step of engine time no matter how
    fast the machine renders. Without it a fast machine finishes all the frames
    before the first scheduled key press is due.

    The demo shows a grey cube - a rigidbody with a box collider - dropped onto
    a ground plate whose mesh collider is the plate's own triangles, under a sky
    the Skybox shader paints, seen through a third-person camera that follows the
    cube and that the pointer turns and tilts. That camera is why the checks are
    split in two halves:

      * the SHOTS prove what a still frame can show - the sky is drawn, the cube
        is grey and on screen in every capture (the camera follows it), the light
        comes from straight above, and the flat interface is drawn on top;
      * the LOG proves what a still frame cannot - the FALL, the BOUNCES and the
        height the cube comes to REST at are the simulation's answers and are read
        out of its own reports; which way the cube MOVED when W/A/S/D was held,
        which way it SPINS after T and that R dropped it again come from the
        demo's state lines; and the camera's own geometry - that it always sits
        exactly where its yaw, its pitch and its distance put it relative to the
        cube, that the yaw covers a full turn and the pitch the full vertical
        range, and that the ground pulled it in rather than letting it pass
        through - is recomputed from those same lines.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\RunAcceptance.ps1 -Configuration Release
#>
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$runDirectory = Join-Path $repositoryRoot "Engine\Intermediate\Binaries\${Configuration}_${Platform}"
$solutionPath = Join-Path $repositoryRoot "VspEngine.slnx"

if (-not $SkipBuild) {
    # MSBuild is located through vswhere (the same lookup VspBuildTool uses);
    # an msbuild on PATH is the fallback.
    $msbuild = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
    }
    if (-not $msbuild) {
        $msbuild = "msbuild"
    }

    Write-Host "Building $Configuration|$Platform ..."
    & $msbuild $solutionPath /restore /p:Configuration=$Configuration /p:Platform=$Platform /v:m /nologo /nodeReuse:false /m:1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "The build failed with exit code $LASTEXITCODE."
        exit 1
    }
}

if (-not (Test-Path (Join-Path $runDirectory "Launch.exe"))) {
    Write-Error "Launch.exe was not found in $runDirectory."
    exit 1
}

Get-ChildItem $runDirectory -Filter "shot*.bmp" -ErrorAction SilentlyContinue | Remove-Item -Force

# The scheduled key presses start 800 ms into the run and each one advances the
# cursor by its hold time plus 300 ms. At 16.6667 ms per frame, one second of
# engine time is 60 frames, and the demo writes a state line every half second,
# so the schedule below leaves a state line inside every phase the checks look
# at - including the very first one, which is what shows the direction the demo
# OPENS with.
#
#   T   0 ms hold   flip the spin to counter-clockwise          t = 800
#   W   1500 ms     move away from the camera (world -Z)       t = 1100 .. 2600
#   D   1500 ms     strafe right (world +X)                    t = 2900 .. 4400
#   A   1500 ms     strafe left (world -X)                     t = 4700 .. 6200
#   T   0 ms hold   flip the spin back to clockwise            t = 6500
#   R   0 ms hold   drop the cube again from its height         t = 6800
#
# The cube is DROPPED at the first frame and again at every R, so the run shows
# the whole simulation: a fall, the bounces restitution asks for, and the rest
# the mesh collider's surface holds it at.
#
# The pointer is moved with six synthetic WM_MOUSEMOVE messages. The first
# sample has no previous position, so only a LATER one produces a delta - which
# is exactly how a physical mouse behaves.
#
#   400,300   t = 6300   the pointer appears (no delta yet)
#   700,300   t = 6400   +300 pixels right: the YAW turns 45 degrees
#   700,667   t = 7800   +367 pixels down: the PITCH tilts to +10 degrees, i.e.
#                        the camera looks up from below the cube - and the ray
#                        from the cube to it now crosses the ground, so the rig
#                        pulls the camera in front of the plate
#   1900,667  t = 8300   +1200 pixels right: the yaw turns another 180 degrees
#                        and WRAPS, which only a full 360-degree orbit does
#   1900,1900 t = 8800   +1233 pixels down: the pitch runs into its +89 stop
#   1900,0    t = 9300   -1900 pixels up: the pitch runs into its -89 stop, so
#                        the range between the two stops is the whole 178 of the
#                        180 degrees a vertical view has
#
# The two axes are moved at different times on purpose, so the state lines prove
# which one each move changed.
#
# The frames the PIXEL checks analyse are the "shot" ones, and they are all taken
# with the camera where the demo opens - above the cube, looking down at it - so
# that what they show can be checked the same way every time. The camera's two
# extremes are captured as "view" frames instead: they are evidence of the range
# the orbit covers (and of the ground pulling the camera in), and the LOG checks
# are what assert on them, because a cube seen from straight below has no lit top
# face to measure and one seen from straight above has nothing else in frame.
#
# 640 frames of 16.6667 ms end at 10667 ms, which covers every key, every mouse
# move and the state lines after them.
# --no-cursor-lock is what makes this run reproducible AND polite: the demo asks
# for a locked pointer (it turns the camera with the mouse), and taking it would
# hide the cursor of whoever is at the machine and move it to the middle of the
# window. With the flag the engine keeps the ordinary pointer and says so, which
# is also what keeps the scheduled pointer positions below meaning exactly what
# they say - a recentred pointer would measure every move from the centre.
$acceptanceArguments = @(
    "--silent", "--frames=640", "--fixed-delta-time=16.6667", "--no-cursor-lock",
    "--key", "0x54:0", "--key", "0x57:1500", "--key", "0x44:1500",
    "--key", "0x41:1500", "--key", "0x54:0", "--key", "0x52:0",
    "--mouse", "400:300:6300", "--mouse", "700:300:6400", "--mouse", "700:667:7800",
    "--mouse", "1900:667:8300", "--mouse", "1900:1900:8800", "--mouse", "1900:0:9300",
    "--capture", "40:shot0_initial.bmp",
    "--capture", "60:shot1_after_T.bmp",
    "--capture", "150:shot2_after_W.bmp",
    "--capture", "235:shot3_after_D.bmp",
    "--capture", "350:shot4_after_A.bmp",
    "--capture", "430:shot5_after_reset.bmp",
    "--capture", "460:shot6_at_rest.bmp",
    "--capture", "500:view_looking_up.bmp",
    "--capture", "560:view_pitch_up.bmp",
    "--capture", "620:view_pitch_down.bmp"
)

# The Vulkan validation layer is what the Debug build runs with; enabling it
# automatically needs the loader to see it: an SDK that was never registered
# machine-wide is only found through VK_LAYER_PATH, which is why the path is set
# here.
$validationLayerCandidates = @()
if ($env:VULKAN_SDK) {
    $validationLayerCandidates += (Join-Path $env:VULKAN_SDK "Bin")
}
$validationLayerCandidates += (Join-Path $repositoryRoot "Engine\Source\Thirdparty\Vulkan\Bin")
$validationLayerCandidates += (Get-ChildItem "C:\VulkanSDK\*\Bin" -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)

foreach ($candidate in $validationLayerCandidates) {
    if ($candidate -and (Test-Path (Join-Path $candidate "VkLayer_khronos_validation.json"))) {
        $env:VK_LAYER_PATH = $candidate
        Write-Host "Vulkan validation layer available through $candidate"
        break
    }
}

# The shader half of the acceptance test: HLSLCC must turn the game's shaders
# into the containers the engine loads.
Write-Host ""
& (Join-Path $PSScriptRoot "VerifyShaderCompilation.ps1") -Configuration $Configuration -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    Write-Error "The shader compilation checks failed."
    exit 1
}
Write-Host ""

Write-Host "Running the acceptance demo from $runDirectory ..."
$process = Start-Process -FilePath (Join-Path $runDirectory "Launch.exe") -ArgumentList $acceptanceArguments -WorkingDirectory $runDirectory -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Write-Error "Launch.exe exited with code $($process.ExitCode)."
    exit 1
}

# ---------------------------------------------------------------------------
# The engine must not have reported anything. This is the check that catches a
# Vulkan validation error, a failed resource creation, a missing shader and a
# broken frame, all at once.
# ---------------------------------------------------------------------------
$runLog = Get-ChildItem $runDirectory -Filter "*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
$engineProblems = Select-String -Path $runLog.FullName -Pattern "\[ERROR\]|\[WARNING\]|\[FATAL\]"
$validationEnabled = Select-String -Path $runLog.FullName -Pattern "Vulkan validation layer enabled"
if ($validationEnabled) {
    Write-Output "Vulkan validation layer: enabled"
} else {
    Write-Output "Vulkan validation layer: not enabled for this configuration"
}
if ($engineProblems) {
    Write-Output "=== ENGINE LOG PROBLEMS ($($runLog.Name)) ==="
    $engineProblems | ForEach-Object { $_.Line }
    Write-Error "The engine logged $($engineProblems.Count) error(s) or warning(s)."
    exit 1
}
Write-Output "Engine log clean: $($runLog.Name)"

& (Join-Path $PSScriptRoot "AnalyzeShots.ps1") -RunDirectory $runDirectory
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# ---------------------------------------------------------------------------
# What the shots cannot show, read out of the run's own records.
# ---------------------------------------------------------------------------
$logText = Get-Content $runLog.FullName -Raw
$behaviourChecks = @()
function Add-BehaviourCheck([string]$name, [bool]$passed) {
    $script:behaviourChecks += [PSCustomObject]@{ Check = $name; Pass = $passed }
}

# ---- The shaders the frame is drawn with -------------------------------------
# The game's two, and the one the ENGINE ships and paints the sky with.
Add-BehaviourCheck "the demo shader 'LitCube' was loaded" ($logText -match "shader 'Assembly/LitCube' ready")
Add-BehaviourCheck "the interface shader 'UiQuad' was loaded" ($logText -match "UiRenderer: shader 'Assembly/UiQuad' ready")
Add-BehaviourCheck "the ENGINE's sky shader was loaded" ($logText -match "Loaded shader 'Vsp/Skybox'")
Add-BehaviourCheck "the engine's sky was ready to be painted" ($logText -match "SkyboxRenderer: the engine's sky is ready")

# ---- The sky is the engine's, and a MATERIAL is what configures it -----------
# The game's render graph has no sky pass at all - the engine paints the sky into
# the frame before the first pass of the game runs - and what the sky IS comes
# from RenderSettings.Skybox, which the demo replaces partway through the run.
Add-BehaviourCheck "the game's render graph has no sky pass of its own" (
    -not ($logText -match "pass 'Sky'|'Sky' \(Raster\)"))
Add-BehaviourCheck "the demo replaced the sky by replacing the material" (
    $logText -match "the sky was replaced at frame [0-9]+ by putting a material of the game's in RenderSettings.Skybox")

# ---- The interface is the ENGINE's, drawn from data and from code -----------
# The UI is not a pass of the game's render graph: the engine draws it into the
# frame it opened (VspEngine.UI.UiSystem). One half of it is a JSON layout the
# ENGINE reads with its own reader, the other is the canvas the demo builds in
# code, and the demo binds to the layout by the names the document gives.
Add-BehaviourCheck "the ENGINE loaded the demo's UI layout itself" (
    $logText -match "UiSystem: the layout '[^']*DemoHud[.]json' is loaded [(][0-9]+ element[(]s[)][)] and is drawn by the engine")
Add-BehaviourCheck "the UI layout went through the engine's JSON reader" (
    $logText -match "[[]JsonExports[]] JSON document '[^']*DemoHud[.]json' parsed")
Add-BehaviourCheck "the game's render graph has no interface pass of its own" (
    -not ($logText -match "'Interface' (Raster)"))

# ---- The engine clock (VspCore's Classes/Time) -------------------------------
# --fixed-delta-time makes the clock advance by exactly one step per frame, and
# the frame-capture diagnostics print the clock's own elapsed time next to the
# frame index. Frame N happens at (N + 1) * step of engine time, because the
# clock is advanced before the frame runs - so the two numbers have to agree.
Add-BehaviourCheck "the engine clock runs on the requested fixed step" ($logText -match "Frame clock: fixed step")
$fixedStepMilliseconds = 16.6667
$clockLines = @(Select-String -Path $runLog.FullName -Pattern "Capture at frame (\d+): .* elapsed=(\d+) ms")
Add-BehaviourCheck "the engine clock reported its elapsed time per capture" ($clockLines.Count -ge 6)
$clockProblems = @()
foreach ($clockLine in $clockLines) {
    $frameIndex = [int]$clockLine.Matches[0].Groups[1].Value
    $elapsedMilliseconds = [int]$clockLine.Matches[0].Groups[2].Value
    $expectedMilliseconds = ($frameIndex + 1) * $fixedStepMilliseconds
    if ([math]::Abs($elapsedMilliseconds - $expectedMilliseconds) -gt 2.0) {
        $clockProblems += "frame $frameIndex reported $elapsedMilliseconds ms instead of $([math]::Round($expectedMilliseconds)) ms"
    }
}
Add-BehaviourCheck "the engine clock's elapsed time matches the frame count" ($clockProblems.Count -eq 0)
if ($clockProblems.Count -gt 0) { $clockProblems | ForEach-Object { Write-Output "    $_" } }

# ---- The frame ran through the render graph ----------------------------------
# The pipeline logs the compiled graph once: every pass with its reads and writes,
# and how many passes the graph culled.
Add-BehaviourCheck "the frame was assembled by the render graph" ($logText -match "RenderGraph")
Add-BehaviourCheck "the render graph culled the switched-off pass" ($logText -match "culled")

# ---- The localisation catalog was read ---------------------------------------
# The demo sets its locale from the operating system, falls back to English and
# reads Locales\<locale>.json next to the executable.
Add-BehaviourCheck "the demo read its translation catalog" ($logText -match "locale '[^']+' with [1-9][0-9]* translation\(s\)")
$localeFiles = @(Get-ChildItem (Join-Path $runDirectory "Locales") -Filter "*.json" -ErrorAction SilentlyContinue)
Add-BehaviourCheck "the game's locale files were staged next to the executable" ($localeFiles.Count -ge 1)

# ---- The physics world: a rigidbody, a collider and a mesh collider ----------
# The simulation reports itself, the ground reports the mesh collider built from
# the plate's own triangles, and the script reports the box collider and the body
# it gave the cube.
Add-BehaviourCheck "the physics world reported itself ready" ($logText -match "Physics world ready \(gravity -9\.81 down")
Add-BehaviourCheck "the ground carries a mesh collider" ($logText -match "ground ready \(mesh collider with 2 triangle\(s\) at y -0\.50\)")
Add-BehaviourCheck "the cube became a physics object" ($logText -match "cube is a physics object \(box collider 1 wide, mass 1")
Add-BehaviourCheck "the cube bounced off the ground" ($logText -match "the cube bounced off the ground \(impact [0-9.]+ down, rebound [0-9.]+ up, bounce [0-9]+\)")

# The collision code answers four questions whose answers are arithmetic - a ray
# into a sphere, a box, a box turned away from the world axes and a mesh - and
# every one of them has to come back with the number the shape implies.
Add-BehaviourCheck "the collision code answered every known-answer check" (
    $logText -match "PhysicsSelfTest: 4 of 4 collision checks passed")

# ---- The pointer: asked for, and left to the user ----------------------------
# The demo turns the camera with the mouse, so it asks for the locked pointer:
# hidden, held inside the window and recentred after every frame. THIS run refuses
# - an automated run must not take the pointer of whoever is at the machine - and
# the engine says so.
Add-BehaviourCheck "the demo asked for the locked pointer, and the engine holds it" (
    $logText -match "CubeController: cursor mode is Locked")
Add-BehaviourCheck "the run left the pointer to the user" (
    $logText -match "This run leaves the pointer to the user")

# ---- The cube: where it is, how fast, which way it spins, where the camera is -
# Every state line looks like:
#   CubeController: cube (0.00, 1.81, 0.00) velocity (0.00, -4.74, 0.00) grounded
#   False spins clockwise about Y; camera yaw 0.00 pitch -45.00 distance 6.50 at
#   (0.00, 6.49, 4.60).
# The number pattern is strict on purpose: a loose "[-0-9.]+" would swallow the
# full stop at the end of the sentence into the last value.
$numberPattern = "-?[0-9]+(?:\.[0-9]+)?"
$statePattern = "CubeController: cube \(($numberPattern), ($numberPattern), ($numberPattern)\) " +
    "velocity \(($numberPattern), ($numberPattern), ($numberPattern)\) " +
    "grounded (True|False) spins (counter-clockwise|clockwise) about Y; " +
    "camera yaw ($numberPattern) pitch ($numberPattern) distance ($numberPattern) " +
    "at \(($numberPattern), ($numberPattern), ($numberPattern)\)"
$stateLines = @(Select-String -Path $runLog.FullName -Pattern $statePattern)
Add-BehaviourCheck "the demo reported its state throughout the run" ($stateLines.Count -ge 12)

if ($stateLines.Count -ge 12) {
    $states = @()
    foreach ($line in $stateLines) {
        $states += [PSCustomObject]@{
            X = [double]$line.Matches[0].Groups[1].Value
            Y = [double]$line.Matches[0].Groups[2].Value
            Z = [double]$line.Matches[0].Groups[3].Value
            VelocityY = [double]$line.Matches[0].Groups[5].Value
            Grounded = ($line.Matches[0].Groups[7].Value -eq "True")
            Spin = $line.Matches[0].Groups[8].Value
            Yaw = [double]$line.Matches[0].Groups[9].Value
            Pitch = [double]$line.Matches[0].Groups[10].Value
            Distance = [double]$line.Matches[0].Groups[11].Value
            CameraX = [double]$line.Matches[0].Groups[12].Value
            CameraY = [double]$line.Matches[0].Groups[13].Value
            CameraZ = [double]$line.Matches[0].Groups[14].Value
        }
    }
    $states | Format-Table -AutoSize

    # ---- The drop: it falls, it lands, it bounces, it comes to rest ----------
    # The cube starts at its drop height with nothing under it, so the first state
    # line has to show it high above the plate and on its way down.
    Add-BehaviourCheck "the cube starts above the ground and falls" (
        $states[0].Y -gt 1.0 -and $states[0].VelocityY -lt -1.0)

    # It reaches the plate. The plate's surface is at GroundSurfaceHeight and half
    # the cube stands above its own centre, so a resting cube's centre is at y = 0
    # - which is the number the collision resolution has to produce.
    Add-BehaviourCheck "the cube landed on the ground plate" (
        @($states | Where-Object { $_.Grounded }).Count -ge 3)

    # It came to REST there: still, grounded, at exactly the height the plate
    # allows. This is the mesh collider's answer - two triangles are what stopped
    # it - and the last state line has to show it.
    $restingStates = @($states | Where-Object {
        [math]::Abs($_.Y) -lt 0.05 -and [math]::Abs($_.VelocityY) -lt 0.05 -and $_.Grounded })
    Add-BehaviourCheck "the cube came to rest on the plate at y = 0.00" (
        $restingStates.Count -ge 4 -and
        [math]::Abs($states[$states.Count - 1].Y) -lt 0.05 -and
        $states[$states.Count - 1].Grounded)

    # R dropped it again: after it had come to rest, a later state line shows it
    # high above the plate once more, and then standing still on it again.
    $firstRestIndex = -1
    for ($stateIndex = 0; $stateIndex -lt $states.Count; $stateIndex++) {
        if ($states[$stateIndex].Grounded -and [math]::Abs($states[$stateIndex].Y) -lt 0.05) {
            $firstRestIndex = $stateIndex
            break
        }
    }
    $reDropIndex = -1
    if ($firstRestIndex -ge 0) {
        for ($stateIndex = $firstRestIndex + 1; $stateIndex -lt $states.Count; $stateIndex++) {
            if ($states[$stateIndex].Y -gt 1.5) { $reDropIndex = $stateIndex; break }
        }
    }
    $restedAgain = $false
    if ($reDropIndex -ge 0) {
        for ($stateIndex = $reDropIndex + 1; $stateIndex -lt $states.Count; $stateIndex++) {
            if ($states[$stateIndex].Grounded -and [math]::Abs($states[$stateIndex].Y) -lt 0.05) { $restedAgain = $true; break }
        }
    }
    Add-BehaviourCheck "R dropped the cube again and it landed a second time" (
        $firstRestIndex -ge 0 -and $reDropIndex -ge 0 -and $restedAgain)

    # ---- The cube is pushed by the keys --------------------------------------
    # W pushed it AWAY from the camera, which with the default yaw looks down -Z.
    Add-BehaviourCheck "W pushed the cube away from the camera (world -Z)" (
        @($states | Where-Object { $_.Z -lt -1.5 }).Count -ge 1)

    # D pushed it to +X, further than W ever pushed it.
    Add-BehaviourCheck "D pushed the cube to +X" (
        @($states | Where-Object { $_.X -gt 2.0 }).Count -ge 1)

    # A pushed it back towards the origin.
    Add-BehaviourCheck "A pushed the cube back towards the origin" (
        $states[$states.Count - 1].X -lt 1.0 -and [math]::Abs($states[$states.Count - 1].Z) -lt 1.0)

    # ---- The spin -------------------------------------------------------------
    # The demo opens SPINNING CLOCKWISE about Y, and the first state line is
    # written before the first T fires, so the opening direction is observable.
    Add-BehaviourCheck "the cube opens spinning clockwise about Y" ($states[0].Spin -eq "clockwise")

    # T flipped the spin to counter-clockwise ...
    Add-BehaviourCheck "T flipped the spin to counter-clockwise" (
        @($states | Where-Object { $_.Spin -eq "counter-clockwise" }).Count -ge 1)

    # ... and the second T flipped it back.
    $clockwiseLines = @($states | Where-Object { $_.Spin -eq "clockwise" })
    Add-BehaviourCheck "the second T flipped the spin back to clockwise" (
        $states[$states.Count - 1].Spin -eq "clockwise" -and $clockwiseLines.Count -ge 2)

    # ---- The camera always looks at the cube ---------------------------------
    # The rig places the camera at "distance" from the cube, along the direction
    # its yaw and pitch give. Three numbers therefore have to agree in EVERY state
    # line: how far the camera is from the cube, how much higher it is, and the
    # angle its pitch reports. A camera that stopped looking at the cube - or a rig
    # whose maths drifted from its own angles - breaks one of them.
    $cameraProblems = @()
    foreach ($state in $states) {
        $offsetX = $state.CameraX - $state.X
        $offsetY = $state.CameraY - $state.Y
        $offsetZ = $state.CameraZ - $state.Z
        $spatialDistance = [math]::Sqrt(($offsetX * $offsetX) + ($offsetY * $offsetY) + ($offsetZ * $offsetZ))
        $heightThePitchAsksFor = -$state.Distance * [math]::Sin($state.Pitch * [math]::PI / 180.0)

        if ([math]::Abs($spatialDistance - $state.Distance) -gt 0.05) {
            $cameraProblems += "the camera is $([math]::Round($spatialDistance, 3)) from the cube but reports $($state.Distance)"
        }
        elseif ([math]::Abs($offsetY - $heightThePitchAsksFor) -gt 0.05) {
            $cameraProblems += "the camera is $([math]::Round($offsetY, 3)) above the cube where pitch $($state.Pitch) asks for $([math]::Round($heightThePitchAsksFor, 3))"
        }
    }
    Add-BehaviourCheck "the camera sits exactly where its yaw, pitch and distance put it" ($cameraProblems.Count -eq 0)
    if ($cameraProblems.Count -gt 0) { $cameraProblems | Select-Object -First 3 | ForEach-Object { Write-Output "    $_" } }

    # The demo opens 45 degrees above the cube.
    Add-BehaviourCheck "the camera opens 45 degrees above the cube" (
        [math]::Abs($states[0].Pitch + 45.0) -lt 0.5)

    # ---- 360 degrees of yaw, 180 degrees of pitch ----------------------------
    # The pointer's horizontal moves turn the yaw by 45 and then by another 180
    # degrees. The angle WRAPS instead of growing, which is what a full turn about
    # the cube looks like from the inside: the last lines are past the 180-degree
    # mark on the other side.
    $yawWithinRange = @($states | Where-Object { $_.Yaw -lt -180.0 -or $_.Yaw -gt 180.0 }).Count -eq 0
    $yawPastHalfTurn = @($states | Where-Object { $_.Yaw -lt -100.0 }).Count -ge 1
    Add-BehaviourCheck "the yaw covers a full turn and wraps instead of growing" (
        $yawWithinRange -and $yawPastHalfTurn -and
        @($states | Where-Object { $_.Yaw -gt 40.0 }).Count -ge 1)

    # The vertical moves run the pitch into BOTH stops, which is the whole 178 of
    # the 180 degrees a vertical view has - and the cube stays in the middle
    # through all of it (the check above).
    Add-BehaviourCheck "the pitch reaches its 89-degree stop looking down" (
        @($states | Where-Object { [math]::Abs($_.Pitch + 89.0) -lt 0.5 }).Count -ge 1)
    Add-BehaviourCheck "the pitch reaches its 89-degree stop looking up" (
        @($states | Where-Object { [math]::Abs($_.Pitch - 89.0) -lt 0.5 }).Count -ge 1)

    # Each axis moves on its own: the lines written between the horizontal move
    # and the vertical one keep the 45 degrees they opened with, and the lines at
    # the end of the vertical move keep the yaw the horizontal one left.
    $turnedOnlyStates = @($states | Where-Object { [math]::Abs($_.Yaw - 45.0) -lt 1.0 -and [math]::Abs($_.Pitch + 45.0) -lt 0.5 })
    Add-BehaviourCheck "a horizontal mouse move leaves the pitch alone" ($turnedOnlyStates.Count -ge 2)

    $tiltedOnlyStates = @($states | Where-Object { [math]::Abs($_.Pitch - 89.0) -lt 0.5 })
    Add-BehaviourCheck "a vertical mouse move leaves the yaw alone" (
        $tiltedOnlyStates.Count -ge 1 -and
        @($tiltedOnlyStates | Where-Object { [math]::Abs($_.Yaw + 135.0) -gt 1.0 }).Count -eq 0)

    # ---- The ground stops the camera, not only the cube ----------------------
    # Looking up from below, the ray from the cube towards where the camera wants
    # to be crosses the ground plate. The rig asks the physics world for what it
    # hit and stops in front of it, so those state lines report a SHORTER distance
    # than the 6.5 the orbit asks for, with the camera still above the surface.
    #
    # The angles checked are the shallow ones: past about 19 degrees the place in
    # front of the plate is closer than the closest distance a camera may come to
    # the object it follows, so the rig stops at that distance instead and the
    # view dips under the plate - a trade-off the rig documents, and not something
    # this check is about.
    $lookingUpStates = @($states | Where-Object { $_.Pitch -gt 5.0 -and $_.Pitch -lt 15.0 })
    Add-BehaviourCheck "the camera was pulled in by the ground it looked through" (
        $lookingUpStates.Count -ge 1 -and
        @($lookingUpStates | Where-Object { $_.Distance -lt 6.4 }).Count -eq $lookingUpStates.Count)
    Add-BehaviourCheck "the pulled-in camera stayed above the ground plate" (
        $lookingUpStates.Count -ge 1 -and
        @($lookingUpStates | Where-Object { $_.CameraY -le -0.5 }).Count -eq 0)
}

$behaviourChecks | Format-Table -AutoSize
$failedBehaviourChecks = @($behaviourChecks | Where-Object { -not $_.Pass })
if ($failedBehaviourChecks.Count -gt 0) {
    Write-Error "$($failedBehaviourChecks.Count) behaviour check(s) failed."
    exit 1
}
Write-Output ("Behaviour: " + $behaviourChecks.Count + "/" + $behaviourChecks.Count + " checks passed")

# ---------------------------------------------------------------------------
# The engine must REFUSE a shader whose bindings are not its own.
#
# The checks above proved HLSLCC writes the engine's numbers into the container.
# This one proves the other half of the contract: a container that names a
# binding the engine does not provide is rejected while loading, by name,
# instead of failing inside the driver.
#
# The binding is patched INSIDE the container - in the reflection record of the
# vertex stage, which is what the engine reads - so the check covers the whole
# path from the bytes on disk to the error message.
# ---------------------------------------------------------------------------
$shaderDirectory = Join-Path $runDirectory "Shaders"
$containerPath = Join-Path $shaderDirectory "LitCube.vsfo"
$backupPath = Join-Path $shaderDirectory "LitCube.original.vsfo"

if (-not (Test-Path $containerPath)) {
    Write-Error "The compiled shader container was not found: $containerPath"
    exit 1
}

# A resource record is Name[64], kind, set, binding, so the binding sits 72 bytes
# into the record. The record itself starts 24 bytes (the reflection header) plus
# one 84-byte variable per input and output into the entry's reflection record -
# NOT at the first occurrence of the name, which is the module's debug info.
$containerBytes = [System.IO.File]::ReadAllBytes($containerPath)
$indexOffset = [System.BitConverter]::ToUInt32($containerBytes, 16)
$entryCount = [System.BitConverter]::ToUInt32($containerBytes, 12)
$dataOffset = [System.BitConverter]::ToUInt32($containerBytes, 24)

$bindingOffset = -1
for ($entryIndex = 0; $entryIndex -lt $entryCount; $entryIndex++) {
    $entryOffset = $indexOffset + ($entryIndex * 324)
    $stageIndex = [System.BitConverter]::ToUInt32($containerBytes, $entryOffset + 0)
    if ($stageIndex -ne 0) { continue }   # 0 = the vertex stage

    $reflectionOffset = [System.BitConverter]::ToUInt32($containerBytes, $entryOffset + 20)
    $inputCount = [System.BitConverter]::ToUInt32($containerBytes, $entryOffset + 28)
    $outputCount = [System.BitConverter]::ToUInt32($containerBytes, $entryOffset + 32)
    $candidateOffset = $dataOffset + $reflectionOffset + 24 + (($inputCount + $outputCount) * 84)

    $name = [System.Text.Encoding]::UTF8.GetString($containerBytes, $candidateOffset, 64).Split([char]0)[0]
    if ($name -eq "CameraUniformBuffer") { $bindingOffset = $candidateOffset + 72; break }
}

if ($bindingOffset -lt 0) {
    Write-Error "The container does not reflect the camera block; the rejection check cannot run."
    exit 1
}

$bindingBytes = [System.BitConverter]::GetBytes([uint32]5)
[System.Array]::Copy($bindingBytes, 0, $containerBytes, $bindingOffset, 4)

Copy-Item $containerPath $backupPath -Force
[System.IO.File]::WriteAllBytes($containerPath, $containerBytes)
try {
    Start-Process -FilePath (Join-Path $runDirectory "Launch.exe") -ArgumentList @("--silent", "--frames=20", "--fixed-delta-time=16.6667") -WorkingDirectory $runDirectory -Wait | Out-Null
    $badLog = Get-ChildItem $runDirectory -Filter "*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
    $rejection = Select-String -Path $badLog.FullName -Pattern "but the engine provides it at set 0 binding 0"
}
finally {
    Move-Item $backupPath $containerPath -Force
}

if (-not $rejection) {
    Write-Error "The engine loaded a shader whose camera block sits at binding 5."
    exit 1
}
Write-Output "The engine refused the tampered shader: $($rejection.Line.Trim())"

# ---------------------------------------------------------------------------
# What the cursor does when a run ALLOWS the pointer to be taken.
#
# The run above refused, which is what keeps its scheduled pointer positions
# meaning exactly what they say. This short one does not refuse, so the demo's
# request reaches the platform: the engine has to report that it hid the pointer
# and held it at the centre of the window, and that it gave the pointer back
# when the window closed - a cursor left hidden would follow the user out.
# ---------------------------------------------------------------------------
Start-Process -FilePath (Join-Path $runDirectory "Launch.exe") -ArgumentList @("--silent", "--frames=30", "--fixed-delta-time=16.6667") -WorkingDirectory $runDirectory -Wait | Out-Null
$cursorLog = Get-ChildItem $runDirectory -Filter "*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
$pointerTaken = Select-String -Path $cursorLog.FullName -Pattern "Cursor locked: the pointer is hidden and held at the centre of the window"
$pointerReturned = Select-String -Path $cursorLog.FullName -Pattern "Cursor released: the pointer is the user's again"

if (-not $pointerTaken) {
    Write-Error "The engine never took the pointer although the game asked for it and the run allowed it."
    exit 1
}
if (-not $pointerReturned) {
    Write-Error "The engine kept the pointer after the game ended."
    exit 1
}
Write-Output "The engine hid the pointer while the game held it, and gave it back: $($pointerTaken.Line.Trim())"

Write-Output ""
Write-Output "Acceptance test passed."