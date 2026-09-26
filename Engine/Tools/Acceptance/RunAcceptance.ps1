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

    The demo shows a lit, per-face coloured cube seen through a third-person
    camera that follows it at 45 degrees. That camera is why the checks are split
    in two halves:

      * the SHOTS prove what a still frame can show - the cube is on screen in
        every capture (the camera follows it), it is colourful, the light comes
        from straight above, and the flat interface is drawn on top of it;
      * the LOG proves what a still frame cannot - a cube that is always centred
        looks the same wherever it is, so which way it MOVED when W/A/S/D was
        held, which way it SPINS after T, and that R put it back, are read out of
        the demo's once-a-second state lines.

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
#   R   0 ms hold   send the cube home                         t = 6800
#
# The pointer is moved with two synthetic WM_MOUSEMOVE messages at t = 6300 and
# t = 6400. The first sample has no previous position, so only the SECOND one
# produces a delta - which is exactly how a physical mouse behaves.
#
# 460 frames of 16.6667 ms end at 7667 ms, which covers the last key, the mouse
# move and the state line after both.
$acceptanceArguments = @(
    "--silent", "--frames=460", "--fixed-delta-time=16.6667",
    "--key", "0x54:0", "--key", "0x57:1500", "--key", "0x44:1500",
    "--key", "0x41:1500", "--key", "0x54:0", "--key", "0x52:0",
    "--mouse", "400:300:6300", "--mouse", "700:300:6400",
    "--capture", "40:shot0_initial.bmp",
    "--capture", "60:shot1_after_T.bmp",
    "--capture", "150:shot2_after_W.bmp",
    "--capture", "235:shot3_after_D.bmp",
    "--capture", "350:shot4_after_A.bmp",
    "--capture", "430:shot5_after_reset.bmp"
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

# ---- The engine loaded the game's two shaders --------------------------------
Add-BehaviourCheck "the demo shader 'LitCube' was loaded" ($logText -match "shader 'Assembly/LitCube' ready")
Add-BehaviourCheck "the interface shader 'UiQuad' was loaded" ($logText -match "UiRenderer: shader 'Assembly/UiQuad' ready")

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

# ---- The cube: where it is, which way it spins, where the camera is ----------
# Every state line looks like:
#   CubeController: cube (0.00, 0.00, 0.00) spins clockwise about Y at 12.34
#   degrees; camera yaw 0.00 pitch -45.00 distance 6.50.
# The number pattern is strict on purpose: a loose "[-0-9.]+" would swallow the
# full stop at the end of the sentence into the last value.
$numberPattern = "-?[0-9]+(?:\.[0-9]+)?"
$statePattern = "CubeController: cube \(($numberPattern), ($numberPattern), ($numberPattern)\) " +
    "spins (counter-clockwise|clockwise) about Y at ($numberPattern) degrees; " +
    "camera yaw ($numberPattern) pitch ($numberPattern) distance ($numberPattern)"
$stateLines = @(Select-String -Path $runLog.FullName -Pattern $statePattern)
Add-BehaviourCheck "the demo reported its state throughout the run" ($stateLines.Count -ge 8)

if ($stateLines.Count -ge 8) {
    $states = @()
    foreach ($line in $stateLines) {
        $states += [PSCustomObject]@{
            X = [double]$line.Matches[0].Groups[1].Value
            Y = [double]$line.Matches[0].Groups[2].Value
            Z = [double]$line.Matches[0].Groups[3].Value
            Spin = $line.Matches[0].Groups[4].Value
            Angle = [double]$line.Matches[0].Groups[5].Value
            Yaw = [double]$line.Matches[0].Groups[6].Value
            Pitch = [double]$line.Matches[0].Groups[7].Value
            Distance = [double]$line.Matches[0].Groups[8].Value
        }
    }
    $states | Format-Table -AutoSize

    # The camera opens 45 degrees above the cube and keeps it centred: the rig
    # places the camera along the direction it looks, so the target IS the cube.
    $badFraming = @($states | Where-Object { [math]::Abs($_.Pitch + 45.0) -gt 1.0 -or [math]::Abs($_.Distance - 6.5) -gt 0.5 })
    Add-BehaviourCheck "the camera stays 45 degrees above the cube at a fixed distance" ($badFraming.Count -eq 0)

    # The cube starts at the origin, spinning the way the demo documents.
    Add-BehaviourCheck "the cube starts at the origin" (
        [math]::Abs($states[0].X) -lt 0.05 -and [math]::Abs($states[0].Y) -lt 0.05 -and [math]::Abs($states[0].Z) -lt 0.05)

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

    # W moved the cube AWAY from the camera, which with the default yaw looks
    # down -Z: the cube's world Z has to become clearly negative.
    Add-BehaviourCheck "W moved the cube away from the camera (world -Z)" (
        @($states | Where-Object { $_.Z -lt -1.5 }).Count -ge 1)

    # D strafed it to +X, further than W ever pushed it.
    Add-BehaviourCheck "D strafed the cube to +X" (
        @($states | Where-Object { $_.X -gt 2.0 }).Count -ge 1)

    # A brought it back towards the origin.
    $afterStrafeX = $states[$states.Count - 1].X
    Add-BehaviourCheck "A moved the cube back towards the origin" ($afterStrafeX -lt 1.0)

    # R put it home.
    Add-BehaviourCheck "R sent the cube back to the origin" (
        [math]::Abs($states[$states.Count - 1].X) -lt 0.05 -and [math]::Abs($states[$states.Count - 1].Z) -lt 0.05)

    # The mouse turned the camera: the pointer moved 300 pixels to the right at
    # 6300 ms, and the demo's sensitivity is 0.15 degrees per pixel, so the yaw
    # of the last state line must have left the 0 it opened with.
    Add-BehaviourCheck "the mouse turned the camera (yaw changed)" (
        [math]::Abs($states[$states.Count - 1].Yaw) -gt 10.0)

    # Moving the pointer horizontally must not tilt the camera: the pitch is
    # still the 45 degrees it opened with.
    Add-BehaviourCheck "a horizontal mouse move leaves the pitch alone" (
        [math]::Abs($states[$states.Count - 1].Pitch + 45.0) -lt 1.0)

    # The cube kept spinning the whole time: the angle never stops growing.
    $spinningThroughout = $true
    for ($stateIndex = 1; $stateIndex -lt $states.Count; $stateIndex++) {
        if ($states[$stateIndex].Angle -eq $states[$stateIndex - 1].Angle) { $spinningThroughout = $false }
    }
    Add-BehaviourCheck "the cube spins continuously" ($spinningThroughout)
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

Write-Output ""
Write-Output "Acceptance test passed."
