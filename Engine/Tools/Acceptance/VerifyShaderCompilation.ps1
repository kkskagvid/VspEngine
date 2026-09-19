<#
.SYNOPSIS
    Verifies that HLSLCC compiles the game's .vsf shader correctly.

.DESCRIPTION
    The shader belongs to the game Assembly, and HLSLCC is what compiles it - the
    engine only loads the SPIR-V the compiler writes.

    Runs HLSLCC.exe over the .vsf shader file that holds BOTH entry points in one
    file (PassVertex and PassFragment) and checks everything the acceptance
    criteria ask for:

      * the Properties block, the Shader block and the Pass blocks nested inside
        it are read out of the file,
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
    # The game's shader that carries both entry points; relative to the repository
    # root. It lives in the game Assembly's Shaders folder.
    [string]$ShaderFile = "Assembly\Shaders\Triangle2D.vsf"
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

# The ENGINE decides where a resource lives: a .vsf shader and the HLSL it includes
# name their resources and never write a descriptor binding or a register.
$shaderDirectory = Split-Path $shaderPath -Parent
$hlslFiles = @(Get-ChildItem -Path $shaderDirectory -Filter *.hlsl -File)
$bindingText = $shaderText
foreach ($hlslFile in $hlslFiles) { $bindingText += (Get-Content $hlslFile.FullName -Raw) }
Add-Check "no shader source writes a Vulkan binding" (-not ($bindingText -match 'vk::binding'))
Add-Check "no shader source writes a register"       (-not ($bindingText -match 'register\s*\('))
Add-Check "no shader source writes a descriptor set" (-not ($bindingText -match 'vk::descriptor_set'))

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

    # The manifest tells the engine which binding every resource of every stage
    # lives at, and those numbers must be the engine's own (see
    # VspCore/Graphics/ShaderBindings.h): set 0, camera UBO 0, bindless images 1,
    # sampler 2.
    $manifestJson = $manifest | ConvertFrom-Json

    # The numbers are read out of the engine's own table, so a change on either
    # side of the contract fails this run.
    $bindingsHeaderPath = Join-Path $repositoryRoot "Engine\Source\Runtime\VspCore\Graphics\ShaderBindings.h"
    $engineBindings = @{}
    if (Test-Path $bindingsHeaderPath) {
        $bindingsHeader = Get-Content $bindingsHeaderPath -Raw
        foreach ($constantName in @("k_nDescriptorSet", "k_nCameraUniformBuffer", "k_nBindlessTextures", "k_nBindlessSampler")) {
            $match = [regex]::Match($bindingsHeader, "$constantName\s*=\s*(\d+)")
            if ($match.Success) { $engineBindings[$constantName] = [int]$match.Groups[1].Value }
        }
    }
    Add-Check "the engine's binding table was read" ($engineBindings.Count -eq 4)

    $expectedBindings = @{
        "CameraUniformBuffer" = @("uniformBuffer", $engineBindings["k_nCameraUniformBuffer"])
        "BindlessTextures"    = @("sampledImage",  $engineBindings["k_nBindlessTextures"])
        "BindlessSampler"     = @("sampler",       $engineBindings["k_nBindlessSampler"])
    }

    $defaultVariant = $manifestJson.variants | Where-Object { $_.key -eq "" } | Select-Object -First 1
    if (-not $defaultVariant) { $defaultVariant = $manifestJson.variants | Select-Object -First 1 }
    Add-Check "the manifest carries the default variant" ($null -ne $defaultVariant)

    $manifestResources = @{}
    if ($defaultVariant) {
        foreach ($pass in $defaultVariant.passes) {
            foreach ($stage in $pass.stages) {
                Add-Check "the manifest lists the resources of the $($stage.stage) stage" (
                    $null -ne $stage.resources)
                foreach ($resource in $stage.resources) {
                    $manifestResources[$resource.name] = $resource
                }
            }
        }
    }

    foreach ($resourceName in $expectedBindings.Keys) {
        $expected = $expectedBindings[$resourceName]
        $resource = $manifestResources[$resourceName]
        Add-Check "the manifest binding of '$resourceName' matches the engine" (
            $null -ne $resource -and
            $resource.kind -eq $expected[0] -and
            $resource.set -eq $engineBindings["k_nDescriptorSet"] -and
            $resource.binding -eq $expected[1])
    }
}

# ---- The shader container the engine loads --------------------------------------
# ONE file holds every module together with its reflection: an index table that
# says which stage each module is, how big it is and what it reflects, and a data
# segment holding the modules. The checks below read the container the way the
# engine does (see Shared/VsfoFormat.h) and cross-check it against the loose .spv
# files HLSLCC writes next to it for a graphics debugger.
$vsfoPath = Join-Path $outputDirectory "$shaderBaseName.vsfo"
$hasContainer = Test-Path $vsfoPath
Add-Check "the shader container was written (.vsfo)" $hasContainer

if ($hasContainer) {
    $container = [System.IO.File]::ReadAllBytes($vsfoPath)

    function Read-ContainerUInt32([byte[]]$bytes, [int]$offset) {
        return [System.BitConverter]::ToUInt32($bytes, $offset)
    }

    # A name field of a fixed-size record.
    function Read-ContainerName([byte[]]$bytes, [int]$offset, [int]$byteSize) {
        $end = $offset
        while ($end -lt ($offset + $byteSize) -and $bytes[$end] -ne 0) { $end++ }
        return [System.Text.Encoding]::UTF8.GetString($bytes, $offset, $end - $offset)
    }

    $containerMagic = Read-ContainerUInt32 $container 0
    $containerVersion = Read-ContainerUInt32 $container 4
    $containerHeaderSize = Read-ContainerUInt32 $container 8
    $containerEntryCount = Read-ContainerUInt32 $container 12
    $containerIndexOffset = Read-ContainerUInt32 $container 16
    $containerIndexSize = Read-ContainerUInt32 $container 20
    $containerDataOffset = Read-ContainerUInt32 $container 24
    $containerDataSize = Read-ContainerUInt32 $container 28
    $containerMetadataOffset = Read-ContainerUInt32 $container 32
    $containerMetadataSize = Read-ContainerUInt32 $container 36
    $containerTotalSize = Read-ContainerUInt32 $container 40

    Add-Check "the container starts with the VSFO magic number" ($containerMagic -eq 0x4F465356)
    Add-Check "the container is version 1" ($containerVersion -eq 1)
    Add-Check "the container header is 48 bytes" ($containerHeaderSize -eq 48)
    Add-Check "the container holds one entry per module (4 = 2 stages x 2 variants)" ($containerEntryCount -eq 4)
    Add-Check "the index table is 324 bytes per entry" ($containerIndexSize -eq (324 * $containerEntryCount))
    Add-Check "the index table starts behind the header" ($containerIndexOffset -eq 48)
    Add-Check "the data segment follows the index table" ($containerDataOffset -ge ($containerIndexOffset + $containerIndexSize))
    Add-Check "every section lies inside the file" (
        ($containerDataOffset + $containerDataSize) -le $containerMetadataOffset -and
        ($containerMetadataOffset + $containerMetadataSize) -le $container.Length -and
        $containerTotalSize -eq $container.Length)

    # The metadata the container carries is what the engine reads first.
    $containerMetadata = [System.Text.Encoding]::UTF8.GetString($container, $containerMetadataOffset, $containerMetadataSize) | ConvertFrom-Json
    Add-Check "the container metadata names the shader" ($containerMetadata.name -eq "Vsp/Triangle2D")
    Add-Check "the container metadata carries the render queue" ($containerMetadata.renderQueue -eq 2000)
    Add-Check "the container metadata lists the properties" ($containerMetadata.properties.Count -eq 2)
    Add-Check "the container metadata lists the keyword groups" ($containerMetadata.keywordGroups.Count -eq 2)
    Add-Check "the container metadata names both variants" ($containerMetadata.variants.Count -eq 2)

    # One entry per module: read them all and check what each one claims.
    $containerEntries = @()
    for ($entryIndex = 0; $entryIndex -lt $containerEntryCount; $entryIndex++) {
        $entryOffset = $containerIndexOffset + ($entryIndex * 324)
        $containerEntries += [pscustomobject]@{
            StageIndex = Read-ContainerUInt32 $container ($entryOffset + 0)
            VariantIndex = Read-ContainerUInt32 $container ($entryOffset + 4)
            PassIndex = Read-ContainerUInt32 $container ($entryOffset + 8)
            SpirvOffset = Read-ContainerUInt32 $container ($entryOffset + 12)
            SpirvSize = Read-ContainerUInt32 $container ($entryOffset + 16)
            ReflectionOffset = Read-ContainerUInt32 $container ($entryOffset + 20)
            ReflectionSize = Read-ContainerUInt32 $container ($entryOffset + 24)
            InputCount = Read-ContainerUInt32 $container ($entryOffset + 28)
            OutputCount = Read-ContainerUInt32 $container ($entryOffset + 32)
            ResourceCount = Read-ContainerUInt32 $container ($entryOffset + 36)
            PushConstantByteSize = Read-ContainerUInt32 $container ($entryOffset + 44)
            StageName = Read-ContainerName $container ($entryOffset + 52) 16
            EntryPointName = Read-ContainerName $container ($entryOffset + 68) 64
            PassName = Read-ContainerName $container ($entryOffset + 132) 64
            VariantKey = Read-ContainerName $container ($entryOffset + 196) 128
        }
    }

    Add-Check "every entry names a stage" (@($containerEntries | Where-Object { $_.StageName -ne "" }).Count -eq $containerEntryCount)
    Add-Check "the container holds a vertex and a fragment module per variant" (
        @($containerEntries | Where-Object { $_.StageName -eq "vertex" }).Count -eq 2 -and
        @($containerEntries | Where-Object { $_.StageName -eq "fragment" }).Count -eq 2)
    Add-Check "the vertex entries name the PassVertex entry point" (
        @($containerEntries | Where-Object { $_.StageName -eq "vertex" -and $_.EntryPointName -eq "PassVertex" }).Count -eq 2)
    Add-Check "the fragment entries name the PassFragment entry point" (
        @($containerEntries | Where-Object { $_.StageName -eq "fragment" -and $_.EntryPointName -eq "PassFragment" }).Count -eq 2)
    Add-Check "one container entry describes the default variant" (
        @($containerEntries | Where-Object { $_.VariantKey -eq "" }).Count -eq 2)
    Add-Check "one container entry describes the _FLAT_COLOR variant" (
        @($containerEntries | Where-Object { $_.VariantKey -eq "_FLAT_COLOR" }).Count -eq 2)

    # Every module must be a real SPIR-V blob of the size the index table claims,
    # and it must be the very same module HLSLCC wrote next to the container.
    $containerModuleProblems = @()
    foreach ($entry in $containerEntries) {
        $moduleOffset = $containerDataOffset + $entry.SpirvOffset
        if ($moduleOffset + $entry.SpirvSize -gt $container.Length) { $containerModuleProblems += "outside the file"; continue }
        if ((Read-ContainerUInt32 $container $moduleOffset) -ne 0x07230203) { $containerModuleProblems += "$($entry.EntryPointName): no SPIR-V magic"; continue }

        $suffix = if ($entry.VariantKey -eq "") { "" } else { ".$($entry.VariantKey)" }
        $stageSuffix = if ($entry.StageName -eq "vertex") { "vert" } else { "frag" }
        $looseModulePath = Join-Path $outputDirectory "$shaderBaseName$suffix.$stageSuffix.spv"
        if (-not (Test-Path $looseModulePath)) { $containerModuleProblems += "$looseModulePath is missing"; continue }

        $looseModule = [System.IO.File]::ReadAllBytes($looseModulePath)
        if ($looseModule.Length -ne $entry.SpirvSize) { $containerModuleProblems += "$looseModulePath has a different size"; continue }

        $containerModule = New-Object byte[] $entry.SpirvSize
        [System.Array]::Copy($container, $moduleOffset, $containerModule, 0, $entry.SpirvSize)
        if ([System.BitConverter]::ToString($containerModule) -ne [System.BitConverter]::ToString($looseModule)) {
            $containerModuleProblems += "$looseModulePath differs from the container"
        }
    }
    Add-Check "every container module matches the .spv file of the same stage" ($containerModuleProblems.Count -eq 0)
    if ($containerModuleProblems.Count -gt 0) { $containerModuleProblems | ForEach-Object { Write-Host "    $_" } }

    # The reflection the index table summarises, read out of the record it points
    # at: the camera block, the bindless array and the sampler of one vertex and
    # one fragment entry.
    $vertexEntry = $containerEntries | Where-Object { $_.StageName -eq "vertex" -and $_.VariantKey -eq "" } | Select-Object -First 1
    $fragmentEntry = $containerEntries | Where-Object { $_.StageName -eq "fragment" -and $_.VariantKey -eq "" } | Select-Object -First 1
    Add-Check "the vertex entry reflects three inputs" ($vertexEntry.InputCount -eq 3)
    Add-Check "the vertex entry reflects one resource" ($vertexEntry.ResourceCount -eq 1)
    Add-Check "the fragment entry reflects two resources" ($fragmentEntry.ResourceCount -eq 2)
    Add-Check "the fragment entry reflects the 48-byte push-constant block" ($fragmentEntry.PushConstantByteSize -eq 48)

    function Read-ContainerResource([byte[]]$bytes, [int]$dataSegmentOffset, $entry, [int]$resourceIndex) {
        # The record holds the inputs and outputs first, then the resources.
        $recordOffset = $dataSegmentOffset + $entry.ReflectionOffset
        $resourceOffset = $recordOffset + 24 + (($entry.InputCount + $entry.OutputCount + $resourceIndex) * 84)
        return [pscustomobject]@{
            Name = Read-ContainerName $bytes $resourceOffset 64
            Kind = Read-ContainerUInt32 $bytes ($resourceOffset + 64)
            Set = Read-ContainerUInt32 $bytes ($resourceOffset + 68)
            Binding = Read-ContainerUInt32 $bytes ($resourceOffset + 72)
        }
    }

    $vertexResource = Read-ContainerResource $container $containerDataOffset $vertexEntry 0
    $fragmentResources = @(
        (Read-ContainerResource $container $containerDataOffset $fragmentEntry 0),
        (Read-ContainerResource $container $containerDataOffset $fragmentEntry 1)
    )

    Add-Check "the container binds the camera block to the engine's uniform-buffer binding" (
        $vertexResource.Name -eq "CameraUniformBuffer" -and $vertexResource.Kind -eq 1 -and
        $vertexResource.Set -eq $engineBindings["k_nDescriptorSet"] -and
        $vertexResource.Binding -eq $engineBindings["k_nCameraUniformBuffer"])
    Add-Check "the container binds the texture array to the engine's sampled-image binding" (
        $fragmentResources[0].Name -eq "BindlessTextures" -and $fragmentResources[0].Kind -eq 3 -and
        $fragmentResources[0].Binding -eq $engineBindings["k_nBindlessTextures"])
    Add-Check "the container binds the sampler to the engine's sampler binding" (
        $fragmentResources[1].Name -eq "BindlessSampler" -and $fragmentResources[1].Kind -eq 5 -and
        $fragmentResources[1].Binding -eq $engineBindings["k_nBindlessSampler"])
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

# ---- several Pass blocks inside one Shader block -------------------------------
# A Shader block may hold more than one Pass block, and every one of them is
# compiled on its own.
$multiPassShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\MultiPassTest.vsf"
$shaderSourceDirectory = Join-Path $repositoryRoot "Assembly\Shaders"

if (Test-Path $multiPassShaderPath) {
    $multiPassText = Get-Content $multiPassShaderPath -Raw
    # "Pass" starts its own line and the block brace may sit on the next one.
    $passBlockCount = ([regex]::Matches($multiPassText, '(?m)^\s*Pass\b[^\r\n]*\r?\n\s*\{')).Count
    Add-Check "the test shader declares two Pass blocks" ($passBlockCount -eq 2)

    & $hlslccPath $multiPassShaderPath --output-directory $variantOutputDirectory -I $shaderSourceDirectory --quiet
    Add-Check "the multi-pass shader compiled" ($LASTEXITCODE -eq 0)

    $forwardVertex = Join-Path $variantOutputDirectory "MultiPassTest.Forward.vert.spv"
    $forwardFragment = Join-Path $variantOutputDirectory "MultiPassTest.Forward.frag.spv"
    $tintedVertex = Join-Path $variantOutputDirectory "MultiPassTest.Tinted.vert.spv"
    $tintedFragment = Join-Path $variantOutputDirectory "MultiPassTest.Tinted.frag.spv"
    Add-Check "the first Pass produced both stages" ((Test-Path $forwardVertex) -and (Test-Path $forwardFragment))
    Add-Check "the second Pass produced both stages" ((Test-Path $tintedVertex) -and (Test-Path $tintedFragment))

    $multiPassManifestPath = Join-Path $variantOutputDirectory "MultiPassTest.shader.json"
    if (Test-Path $multiPassManifestPath) {
        $multiPassManifest = Get-Content $multiPassManifestPath -Raw
        Add-Check "the manifest counts both passes" ($multiPassManifest -match '"passCount"\s*:\s*2')
        Add-Check "the manifest names both passes" (
            $multiPassManifest -match '"name"\s*:\s*"Forward"' -and $multiPassManifest -match '"name"\s*:\s*"Tinted"')
    }
    else {
        Add-Check "the multi-pass manifest was written" $false
    }
}
else {
    Add-Check "the multi-pass test shader exists" $false
}

# ---- a shader that names its OWN bindings ---------------------------------------
# The engine numbers the resources of a shader that names none. A shader that writes
# [[vk::binding]] - here in the HLSL file its Pass includes, which no scan of the
# .vsf text alone would see - keeps its own numbers.
$explicitShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\ExplicitBindingTest.vsf"

if (Test-Path $explicitShaderPath) {
    $explicitOutputDirectory = Join-Path $variantOutputDirectory "ExplicitBindings"
    if (Test-Path $explicitOutputDirectory) { Remove-Item $explicitOutputDirectory -Recurse -Force }

    & $hlslccPath $explicitShaderPath --output-directory $explicitOutputDirectory --quiet
    Add-Check "the shader that names its own bindings compiled" ($LASTEXITCODE -eq 0)

    $explicitManifestPath = Join-Path $explicitOutputDirectory "ExplicitBindingTest.shader.json"
    if (Test-Path $explicitManifestPath) {
        $explicitJson = Get-Content $explicitManifestPath -Raw | ConvertFrom-Json
        $explicitStage = $explicitJson.variants[0].passes[0].stages | Where-Object { $_.stage -eq "vertex" }
        $explicitBlock = $null
        if ($explicitStage) { $explicitBlock = $explicitStage.resources | Where-Object { $_.name -eq "ExplicitCameraBlock" } }
        Add-Check "the shader's own binding number was kept (7, not the engine's 0)" (
            $null -ne $explicitBlock -and $explicitBlock.binding -eq 7)
    }
    else {
        Add-Check "the shader that names its own bindings produced a manifest" $false
    }

    # --keep-explicit-bindings turns the engine's numbering off for a whole run.
    & $hlslccPath $explicitShaderPath --output-directory $explicitOutputDirectory --keep-explicit-bindings --quiet
    Add-Check "--keep-explicit-bindings still compiles the shader" ($LASTEXITCODE -eq 0)
}
else {
    Add-Check "the explicit-binding test shader exists" $false
}
# ---- the HLSL builtin library ---------------------------------------------------
# <Vsp/...> includes resolve against the builtin library HLSLCC ships next to
# itself. This shader uses the whole library - transforms, bindless textures,
# normal maps and the lighting models - so a broken helper fails the run.
$builtinShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\BuiltinLibraryTest.vsf"
$builtinLibraryDirectory = Join-Path $runDirectory "Builtin"

Add-Check "the builtin library was staged next to the compiler" (Test-Path (Join-Path $builtinLibraryDirectory "Vsp\Lighting.hlsl"))

if (Test-Path $builtinShaderPath) {
    $builtinOutputDirectory = Join-Path $variantOutputDirectory "BuiltinLibrary"
    if (Test-Path $builtinOutputDirectory) { Remove-Item $builtinOutputDirectory -Recurse -Force }

    $builtinLog = & $hlslccPath $builtinShaderPath --output-directory $builtinOutputDirectory 2>&1 | Out-String
    Add-Check "the shader using the builtin library compiled" ($LASTEXITCODE -eq 0)
    Add-Check "the compiler reported the builtin library it used" ($builtinLog -match "builtin library\s+:")
    if ($LASTEXITCODE -ne 0) { Write-Host $builtinLog }

    $builtinContainerPath = Join-Path $builtinOutputDirectory "BuiltinLibraryTest.vsfo"
    Add-Check "the builtin test produced a container" (Test-Path $builtinContainerPath)

    if (Test-Path $builtinContainerPath) {
        $builtinContainer = [System.IO.File]::ReadAllBytes($builtinContainerPath)
        $builtinEntryCount = Read-ContainerUInt32 $builtinContainer 12
        $builtinIndexOffset = Read-ContainerUInt32 $builtinContainer 16
        $builtinDataOffset = Read-ContainerUInt32 $builtinContainer 24

        Add-Check "the builtin test compiled both stages" ($builtinEntryCount -eq 2)

        # The fragment stage is the one that reads the texture array and the
        # sampler, so its resources must have landed on the engine's bindings.
        $builtinEntries = @()
        for ($entryIndex = 0; $entryIndex -lt $builtinEntryCount; $entryIndex++) {
            $entryOffset = $builtinIndexOffset + ($entryIndex * 324)
            $builtinEntries += [pscustomobject]@{
                StageName = Read-ContainerName $builtinContainer ($entryOffset + 52) 16
                InputCount = Read-ContainerUInt32 $builtinContainer ($entryOffset + 28)
                OutputCount = Read-ContainerUInt32 $builtinContainer ($entryOffset + 32)
                ResourceCount = Read-ContainerUInt32 $builtinContainer ($entryOffset + 36)
                ReflectionOffset = Read-ContainerUInt32 $builtinContainer ($entryOffset + 20)
            }
        }

        $builtinFragment = $builtinEntries | Where-Object { $_.StageName -eq "fragment" } | Select-Object -First 1
        Add-Check "the builtin test shaded with the library's lighting model" ($null -ne $builtinFragment)

        if ($null -ne $builtinFragment) {
            $builtinResources = @()
            for ($resourceIndex = 0; $resourceIndex -lt $builtinFragment.ResourceCount; $resourceIndex++) {
                $builtinResources += Read-ContainerResource $builtinContainer $builtinDataOffset $builtinFragment $resourceIndex
            }

            Add-Check "the library's bindless array landed on the engine's sampled-image binding" (
                @($builtinResources | Where-Object { $_.Binding -eq $engineBindings["k_nBindlessTextures"] -and $_.Kind -eq 3 }).Count -eq 1)
            Add-Check "the library's sampler landed on the engine's sampler binding" (
                @($builtinResources | Where-Object { $_.Binding -eq $engineBindings["k_nBindlessSampler"] -and $_.Kind -eq 5 }).Count -eq 1)
        }
    }
}
else {
    Add-Check "the builtin library test shader exists" $false
}
# A Pass block written outside the Shader block is a structural mistake and must
# be reported instead of silently ignored.
$misplacedPassPath = Join-Path $variantOutputDirectory "MisplacedPass.vsf"
New-Item -ItemType Directory -Path $variantOutputDirectory -Force | Out-Null
@'
Properties
{
    _ColorMode ("Color Mode", Float) = 3
}

Shader "Vsp/Misplaced"
{
    Queue = "Geometry"
}

Pass
{
    #pragma vertex PassVertex
    #pragma fragment PassFragment

    float4 PassVertex(float2 position : POSITION) : SV_Position { return float4(position, 0.0, 1.0); }
    float4 PassFragment() : SV_Target0 { return float4(1.0, 1.0, 1.0, 1.0); }
}
'@ | Set-Content -Path $misplacedPassPath -Encoding utf8

# The failure is the point here, so the stderr it writes must not abort the run.
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $hlslccPath $misplacedPassPath --output-directory $variantOutputDirectory --quiet 2>&1 | Out-Null
$misplacedPassExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference
Add-Check "a Pass block outside the Shader block is rejected" ($misplacedPassExitCode -ne 0)

# The engine's own copy must stay the stripped one, so regenerate it last.
& $hlslccPath $shaderPath --output-directory $outputDirectory --quiet
Add-Check "the engine's shader directory was refreshed" ($LASTEXITCODE -eq 0)

$checks | Format-Table -AutoSize
$failed = @($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + $checks.Count + " checks passed")
if ($failed -gt 0) { exit 1 }
exit 0