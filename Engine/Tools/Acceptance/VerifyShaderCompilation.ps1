<#
.SYNOPSIS
    Verifies that HLSLCC compiles the engine's .vsf shader correctly.

.DESCRIPTION
    Runs HLSLCC.exe over the .vsf shader file that holds BOTH entry points in one
    file (PassVertex and PassFragment) and checks everything the acceptance
    criteria ask for:

      * the Properties, Shader and Pass blocks are read out of the file,
      * one SPIR-V binary per stage per kept variant is written, each starting
        with the SPIR-V magic number and carrying its entry point,
      * the reflection document describes both stages, their interface
        variables, their descriptor bindings and the push-constant layout,
      * the shader manifest the ENGINE loads names those modules,
      * #pragma variant strips the variants nobody uses while
        #pragma multi_variant keeps every one of them,
      * the modules are valid SPIR-V (spirv-val, when the SDK ships it).

    The compiled output is written into the run directory's Shaders folder, which
    is where the engine loads its shaders from - the same command therefore both
    compiles and installs the shader the demo runs.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1 -Configuration Release
#>
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    # Shader that carries both entry points; relative to the repository root.
    [string]$ShaderFile = "Engine\Source\Runtime\VspCore\Graphics\Shaders\Triangle2D.vsf"
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$runDirectory = Join-Path $repositoryRoot "Engine\Intermediate\Binaries\${Configuration}_${Platform}"
$hlslccPath = Join-Path $runDirectory "HLSLCC.exe"
$shaderPath = Join-Path $repositoryRoot $ShaderFile
$shaderBaseName = [System.IO.Path]::GetFileNameWithoutExtension($shaderPath)
$outputDirectory = Join-Path $runDirectory "Shaders"

# Scratch directory for the variant comparison runs, so the engine's own
# compiled shader is never disturbed.
$variantOutputDirectory = Join-Path $repositoryRoot "Engine\Intermediate\ShaderVariants"

$checks = @()
function Add-Check([string]$name, [bool]$passed)
{
    $script:checks += [PSCustomObject]@{ Check = $name; Pass = $passed }
}

if (-not (Test-Path $hlslccPath)) {
    Write-Error "HLSLCC.exe was not found in $runDirectory. Build the solution first."
    exit 1
}
if (-not (Test-Path $shaderPath)) {
    Write-Error "The shader file was not found: $shaderPath"
    exit 1
}

# ---- The .vsf file carries everything ------------------------------------------
$shaderText = Get-Content $shaderPath -Raw
Add-Check "the shader file has a Properties block" ($shaderText -match '(?m)^\s*Properties\s*\{')
Add-Check "the shader file has a Shader block"     ($shaderText -match '(?m)^\s*Shader\s+"')
Add-Check "the shader file has a Pass block"       ($shaderText -match '(?m)^\s*Pass\s*\{')
Add-Check "the shader file names PassVertex"       ($shaderText -match '#pragma\s+vertex\s+PassVertex')
Add-Check "the shader file names PassFragment"     ($shaderText -match '#pragma\s+fragment\s+PassFragment')
Add-Check "the Pass block includes an HLSL file"   ($shaderText -match '#include\s+"[^"]+\.hlsl"')
Add-Check "the shader declares a strippable variant"     ($shaderText -match '(?m)^\s*Variant\s+_|#pragma\s+variant\s+_')
Add-Check "the shader declares an always-kept variant"   ($shaderText -match '#pragma\s+multi_variant')

if (Test-Path $outputDirectory) {
    Remove-Item $outputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

Write-Host "HLSLCC: $shaderPath"
& $hlslccPath $shaderPath --output-directory $outputDirectory
$compileExitCode = $LASTEXITCODE
Add-Check "HLSLCC exited successfully (0)" ($compileExitCode -eq 0)
if ($compileExitCode -ne 0) {
    $checks | Format-Table -AutoSize
    Write-Error "HLSLCC failed with exit code $compileExitCode."
    exit 1
}

# ---- One SPIR-V binary per stage of the default variant -------------------------
$spirvFiles = @{
    vertex   = Join-Path $outputDirectory "$shaderBaseName.vert.spv"
    fragment = Join-Path $outputDirectory "$shaderBaseName.frag.spv"
}

foreach ($stage in @("vertex", "fragment")) {
    $spirvPath = $spirvFiles[$stage]
    $exists = Test-Path $spirvPath
    Add-Check "the default variant's $stage module was written" $exists
    if (-not $exists) {
        continue
    }

    $bytes = [System.IO.File]::ReadAllBytes($spirvPath)
    Add-Check "the $stage binary is a non-empty SPIR-V module" ($bytes.Length -ge 20 -and ($bytes.Length % 4) -eq 0)
    $magic = [System.BitConverter]::ToUInt32($bytes, 0)
    Add-Check "the $stage binary starts with the SPIR-V magic number" ($magic -eq 0x07230203)

    $spirvText = -join ($bytes | ForEach-Object { [char]$_ })
    $expectedEntryPoint = if ($stage -eq "vertex") { "PassVertex" } else { "PassFragment" }
    Add-Check "the $stage module carries the '$expectedEntryPoint' entry point" ($spirvText -match $expectedEntryPoint)

    $spirvVal = $null
    $vulkanSdkRoot = $env:VULKAN_SDK
    if ($vulkanSdkRoot) {
        $candidate = Join-Path $vulkanSdkRoot "Bin\spirv-val.exe"
        if (Test-Path $candidate) { $spirvVal = $candidate }
    }
    if (-not $spirvVal) {
        $candidate = Get-ChildItem "C:\VulkanSDK\*\Bin\spirv-val.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($candidate) { $spirvVal = $candidate.FullName }
    }
    if ($spirvVal) {
        & $spirvVal "--target-env" "vulkan1.3" $spirvPath | Out-Null
        Add-Check "spirv-val accepts the $stage module" ($LASTEXITCODE -eq 0)
    }
}

# ---- The always-kept variant was compiled too ----------------------------------
$flatColorModules = Get-ChildItem $outputDirectory -Filter "$shaderBaseName._*.spv" -ErrorAction SilentlyContinue
Add-Check "the always-kept variant was compiled as well" ($flatColorModules.Count -ge 2)

# ---- Reflection ---------------------------------------------------------------
$reflectionPath = Join-Path $outputDirectory "$shaderBaseName.reflection.json"
$hasReflection = Test-Path $reflectionPath
Add-Check "the reflection document was written" $hasReflection

if ($hasReflection) {
    $reflection = Get-Content $reflectionPath -Raw
    Add-Check "the reflection describes the vertex stage" ($reflection -match '"stage"\s*:\s*"vertex"')
    Add-Check "the reflection describes the fragment stage" ($reflection -match '"stage"\s*:\s*"fragment"')
    Add-Check "the reflection names the PassVertex entry point" ($reflection -match '"entryPoint"\s*:\s*"PassVertex"')
    Add-Check "the reflection names the PassFragment entry point" ($reflection -match '"entryPoint"\s*:\s*"PassFragment"')
    Add-Check "the reflection lists the three vertex inputs" ($reflection -match '"inputCount"\s*:\s*3')
    Add-Check "the reflection lists the camera uniform buffer" ($reflection -match '"kind"\s*:\s*"uniformBuffer"')
    Add-Check "the reflection lists the bindless sampled-image array" ($reflection -match '"kind"\s*:\s*"sampledImage"')
    Add-Check "the reflection lists the bindless sampler" ($reflection -match '"kind"\s*:\s*"sampler"')
    Add-Check "the push-constant block is 48 bytes" ($reflection -match '"pushConstantByteSize"\s*:\s*48')
    Add-Check "the push-constant block has five members" ($reflection -match '"pushConstantMemberCount"\s*:\s*5')
}

# ---- The manifest the engine loads ---------------------------------------------
$manifestPath = Join-Path $outputDirectory "$shaderBaseName.shader.json"
$hasManifest = Test-Path $manifestPath
Add-Check "the shader manifest was written" $hasManifest

if ($hasManifest) {
    $manifest = Get-Content $manifestPath -Raw
    Add-Check "the manifest names the shader" ($manifest -match '"name"\s*:\s*"Vsp/Triangle2D"')
    Add-Check "the manifest carries the render queue" ($manifest -match '"renderQueue"\s*:\s*2000')
    Add-Check "the manifest lists the shader properties" ($manifest -match '"name"\s*:\s*"_ColorMode"' -and $manifest -match '"name"\s*:\s*"_Tint"')
    Add-Check "the manifest lists the keyword groups" ($manifest -match '"kind"\s*:\s*"multi_variant_local"' -and $manifest -match '"kind"\s*:\s*"variant"')
    Add-Check "the manifest names the compiled module of every stage" (
        $manifest -match '"file"\s*:\s*"Triangle2D\.vert\.spv"' -and $manifest -match '"file"\s*:\s*"Triangle2D\.frag\.spv"')

    # Every module the manifest names must exist next to it.
    $moduleMatches = [regex]::Matches($manifest, '"file"\s*:\s*"([^"]+)"')
    $missingModules = @()
    foreach ($moduleMatch in $moduleMatches) {
        $modulePath = Join-Path $outputDirectory $moduleMatch.Groups[1].Value
        if (-not (Test-Path $modulePath)) { $missingModules += $moduleMatch.Groups[1].Value }
    }
    Add-Check "every module the manifest names exists" ($missingModules.Count -eq 0)
}

# ---- variant vs multi_variant ---------------------------------------------------
# Without any --used-variant the strippable group keeps only its default state,
# while the multi_variant group keeps every state.
if (Test-Path $variantOutputDirectory) {
    Remove-Item $variantOutputDirectory -Recurse -Force
}
& $hlslccPath $shaderPath --output-directory $variantOutputDirectory --quiet
Add-Check "the stripped build succeeded" ($LASTEXITCODE -eq 0)

$strippedManifest = Get-Content (Join-Path $variantOutputDirectory "$shaderBaseName.shader.json") -Raw
$strippedVariantCount = [int]([regex]::Match($strippedManifest, '"variantCount"\s*:\s*(\d+)').Groups[1].Value)
Add-Check "unused strippable variants are removed (2 = default x multi_variant)" ($strippedVariantCount -eq 2)

# Reporting the keyword as used swaps which state of the strippable group ships:
# the build then carries the tinted variant instead of the untinted one.
& $hlslccPath $shaderPath --output-directory $variantOutputDirectory --used-variant _TINT_ENABLED --quiet
Add-Check "the variant build succeeded" ($LASTEXITCODE -eq 0)

$usedManifest = Get-Content (Join-Path $variantOutputDirectory "$shaderBaseName.shader.json") -Raw
$usedVariantCount = [int]([regex]::Match($usedManifest, '"variantCount"\s*:\s*(\d+)').Groups[1].Value)
Add-Check "the reported variant replaced the default one (2 = 1 tint state x 2 multi_variant states)" ($usedVariantCount -eq 2)
Add-Check "the kept variant names its keyword" ($usedManifest -match '"key"\s*:\s*"_TINT_ENABLED"')

# Reporting both states of the strippable group keeps both of them.
& $hlslccPath $shaderPath --output-directory $variantOutputDirectory --used-variant _ --used-variant _TINT_ENABLED --quiet
Add-Check "the two-state build succeeded" ($LASTEXITCODE -eq 0)

$twoStateManifest = Get-Content (Join-Path $variantOutputDirectory "$shaderBaseName.shader.json") -Raw
$twoStateVariantCount = [int]([regex]::Match($twoStateManifest, '"variantCount"\s*:\s*(\d+)').Groups[1].Value)
Add-Check "both reported states are kept (4 = 2 tint states x 2 multi_variant states)" ($twoStateVariantCount -eq 4)

# The engine's own copy must stay the stripped one, so regenerate it last.
& $hlslccPath $shaderPath --output-directory $outputDirectory --quiet
Add-Check "the engine's shader directory was refreshed" ($LASTEXITCODE -eq 0)

$checks | Format-Table -AutoSize
$failed = ($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + $checks.Count + " checks passed")
if ($failed -gt 0) { exit 1 }
exit 0
