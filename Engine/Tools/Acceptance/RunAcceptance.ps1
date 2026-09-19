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
    fast the machine renders. Without it a fast machine finishes all 445 frames
    before the first scheduled key press is due.

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

# The scheduled key presses start 800 ms into the run and the last one fires at
# 7100 ms, so the run has to cover that much engine time: 445 frames of 16.6667
# ms end at 7417 ms.
$acceptanceArguments = @(
    "--silent", "--frames=445", "--fixed-delta-time=16.6667",
    "--key", "0x44:1500", "--key", "0x57:800", "--key", "0x41:800", "--key", "0x53:800", "--key", "0x52:0",
    "--key", "0x54:0", "--key", "0x54:0", "--key", "0x54:0", "--key", "0x54:0",
    "--capture", "40:shot0_initial.bmp", "--capture", "148:shot1_after_D.bmp",
    "--capture", "210:shot2_after_W.bmp", "--capture", "276:shot3_after_A.bmp",
    "--capture", "342:shot4_after_S.bmp", "--capture", "358:shot5_after_R.bmp",
    "--capture", "376:shot6_T_red.bmp", "--capture", "394:shot7_T_blue.bmp",
    "--capture", "412:shot8_T_green.bmp", "--capture", "430:shot9_T_multi.bmp"
)

# Make the Khronos validation layer visible to the Vulkan loader. The engine
# enables it automatically in Debug builds, but only when the loader can see it:
# an SDK that was never registered machine-wide is only found through
# VK_LAYER_PATH, which is why the path is set here.
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

# The shader half of the acceptance test: HLSLCC must turn the shader file that
# holds both entry points into one SPIR-V binary per stage plus reflection.
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

# The engine writes one log file per run, named after its timestamp.
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
$containerPath = Join-Path $shaderDirectory "Triangle2D.vsfo"
$backupPath = Join-Path $shaderDirectory "Triangle2D.original.vsfo"

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



