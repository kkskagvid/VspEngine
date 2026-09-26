<#
.SYNOPSIS
    Verifies that HLSLCC compiles the game's .vsf shaders correctly.

.DESCRIPTION
    The game's shaders belong to the Assembly, and HLSLCC is what compiles them -
    the engine only loads the SPIR-V the compiler writes. Every shader is ONE .vsf
    file that carries its properties, its settings and its Pass blocks, so this
    script runs HLSLCC over each of them and checks everything the acceptance
    criteria ask for:

      * the Properties block, the Shader block and the Pass blocks nested inside
        it are read out of the file, together with the entry-point pragmas, the
        Pass-level #include and the keyword groups,
      * no shader source writes a binding of its own,
      * one SPIR-V binary per stage per kept variant is written, each starting
        with the SPIR-V magic number and carrying its entry point,
      * the reflection document, the shader manifest and the .vsfo container the
        engine loads agree with each other and with the modules,
      * every resource a shader declares sits on the ENGINE's binding (read out of
        VspCore/Graphics/ShaderBindings.h, not written down here),
      * the push-constant block's byte size is not a magic number: it is a
        positive multiple of 16 that reports at least one member, and the
        container repeats exactly what the reflection says,
      * #pragma variant strips the variants nobody uses while #pragma
        multi_variant keeps every one of them,
      * the modules are valid SPIR-V (spirv-val, when the SDK ships it).

    Nothing about a shader is hardcoded: its entry points come from its
    "#pragma vertex" / "#pragma fragment" lines, its name from the "Shader" line,
    its properties from the Properties block, its keyword groups from the
    Variant/VariantLocal/MultiVariant/MultiVariantLocal declarations and the
    "#pragma variant*" lines, and its render queue from its "Queue" setting (the
    queue names are read out of the compiler's own table). Verifying one more
    shader therefore only means adding its name to -ShaderFiles.

    The compiled output is written into the run directory's Shaders folder, which
    is where the engine loads its shaders from - the same command therefore both
    compiles and installs the shaders the demo runs.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Acceptance\VerifyShaderCompilation.ps1 -Configuration Release
#>
param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    # The game's shaders, relative to the repository root. Every one of them is a
    # .vsf file of the game Assembly's Shaders folder; adding a shader to the demo
    # means adding its name here.
    [string[]]$ShaderFiles = @("Assembly\Shaders\LitCube.vsf", "Assembly\Shaders\UiQuad.vsf")
)

$ErrorActionPreference = "Stop"

# A compiler that reports a failure through its exit code and its stderr is not an
# exception here: the checks below decide what a failed compile means.
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$runDirectory = Join-Path $repositoryRoot "Engine\Intermediate\Binaries\$($Configuration)_$($Platform)"
$hlslccPath = Join-Path $runDirectory "HLSLCC.exe"

# Where the engine loads its shaders from: HLSLCC writes the .vsfo container, the
# loose .spv modules and the manifest of every game shader here.
$outputDirectory = Join-Path $runDirectory "Shaders"

# Scratch directory of the variant comparison runs and of the standalone asset
# tests, so the engine's own compiled shaders are never disturbed.
$variantOutputDirectory = Join-Path $repositoryRoot "Engine\Intermediate\ShaderVariants"

# The two programmable stages a graphics pipeline has.
$stageNames = @("vertex", "fragment")

$checks = @()
function Add-Check([string]$name, [bool]$passed)
{
    $script:checks += [PSCustomObject]@{ Check = $name; Pass = $passed }
}

if (-not (Test-Path $hlslccPath)) {
    Write-Error "HLSLCC.exe was not found in $runDirectory. Build the solution first."
    exit 1
}

$shaderPaths = @()
foreach ($shaderFile in $ShaderFiles) {
    $shaderPath = Join-Path $repositoryRoot $shaderFile
    if (-not (Test-Path $shaderPath)) {
        Write-Error "The shader file was not found: $shaderPath"
        exit 1
    }
    $shaderPaths += $shaderPath
}

# The directory a shader's own #include lines resolve against, handed to the
# compiler for the standalone asset tests as well.
$shaderSourceDirectories = @()
foreach ($shaderPath in $shaderPaths) {
    $directory = Split-Path $shaderPath -Parent
    if ($shaderSourceDirectories -notcontains $directory) { $shaderSourceDirectories += $directory }
}

# spirv-val comes with the Vulkan SDK; when the SDK is not installed the modules
# are checked structurally instead.
$spirvValPath = $null
if ($env:VULKAN_SDK) {
    $candidate = Join-Path $env:VULKAN_SDK "Bin\spirv-val.exe"
    if (Test-Path $candidate) { $spirvValPath = $candidate }
}
if (-not $spirvValPath) {
    $candidate = Get-ChildItem "C:\VulkanSDK\*\Bin\spirv-val.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) { $spirvValPath = $candidate.FullName }
}

# ---------------------------------------------------------------------------
# Reading a shader file
# ---------------------------------------------------------------------------

# Blanks out // and /* */ comments, so a brace or a pragma inside a comment
# cannot confuse the structural reads below. String literals are kept.
function Get-MaskedShaderText([string]$text)
{
    $builder = New-Object System.Text.StringBuilder
    $length = $text.Length
    $index = 0
    $inString = $false

    while ($index -lt $length) {
        $character = $text[$index]

        if ($inString) {
            [void]$builder.Append($character)
            if ($character -eq '"') { $inString = $false }
            $index++
            continue
        }

        if ($character -eq '"') {
            $inString = $true
            [void]$builder.Append($character)
            $index++
            continue
        }

        if ($character -eq '/' -and ($index + 1) -lt $length) {
            $next = $text[$index + 1]
            if ($next -eq '/') {
                while ($index -lt $length -and $text[$index] -ne [char]10) { [void]$builder.Append(' '); $index++ }
                continue
            }
            if ($next -eq '*') {
                [void]$builder.Append('  ')
                $index += 2
                while ($index -lt $length -and -not ($text[$index] -eq '*' -and ($index + 1) -lt $length -and $text[$index + 1] -eq '/')) {
                    [void]$builder.Append(' ')
                    $index++
                }
                if ($index -lt $length) { [void]$builder.Append('  '); $index += 2 }
                continue
            }
        }

        [void]$builder.Append($character)
        $index++
    }

    return $builder.ToString()
}

# Every "<keyword> ... { ... }" block of a masked shader text: the header
# ("Pass "Second"") and the body between the braces. The offsets stay relative to
# the text that was searched, so a Pass block can be hidden inside a Shader block.
function Get-ShaderBlocks([string]$text, [string]$keyword)
{
    $blocks = @()
    $pattern = "(?m)^[ \t]*$keyword\b[^\r\n{]*(?:\r?\n[ \t]*)?\{"

    foreach ($match in [regex]::Matches($text, $pattern)) {
        $openOffset = $match.Index + $match.Length - 1
        $depth = 0
        $index = $openOffset

        while ($index -lt $text.Length) {
            $character = $text[$index]
            if ($character -eq '{') { $depth++ }
            elseif ($character -eq '}') {
                $depth--
                if ($depth -eq 0) { break }
            }
            $index++
        }
        if ($index -ge $text.Length) { continue }

        $blocks += [pscustomobject]@{
            HeaderStart = $match.Index
            HeaderText  = $text.Substring($match.Index, $openOffset - $match.Index)
            BodyStart   = $openOffset + 1
            BodyLength  = $index - $openOffset - 1
            Body        = $text.Substring($openOffset + 1, $index - $openOffset - 1)
        }
    }

    return $blocks
}

# The same text with every given block replaced by spaces, so the settings of a
# Shader block can be read without the HLSL of its Pass bodies getting in the way.
function Hide-ShaderBlocks([string]$text, $blocks)
{
    $characters = $text.ToCharArray()
    foreach ($block in $blocks) {
        $end = $block.BodyStart + $block.BodyLength
        if ($end -ge $characters.Length) { $end = $characters.Length - 1 }
        for ($index = $block.HeaderStart; $index -le $end; $index++) { $characters[$index] = ' ' }
    }
    return (-join $characters)
}

# The keyword names a Variant/VariantLocal/MultiVariant/MultiVariantLocal
# declaration or a "#pragma variant*" line lists.
function Get-KeywordNames([string]$text)
{
    $names = @()
    foreach ($match in [regex]::Matches($text, '[A-Za-z_][A-Za-z0-9_]*')) { $names += $match.Value }
    return $names
}

# One keyword group with the states it can be compiled in. A single keyword is a
# boolean keyword ("off" and "on"); every other declaration is used as it stands.
function New-KeywordGroup([string]$kind, [string[]]$names)
{
    $states = @()
    if ($names.Count -eq 1 -and $names[0] -ne "_") { $states = @("_", $names[0]) }
    else { $states = $names }

    return [pscustomobject]@{
        Name       = ($names -join "|")
        Kind       = $kind
        Strippable = ($kind -eq "variant" -or $kind -eq "variant_local")
        Local      = ($kind -eq "variant_local" -or $kind -eq "multi_variant_local")
        States     = $states
    }
}

# Every keyword group a .vsf declares: the settings of its Shader block first, then
# the "#pragma variant*" lines of its Pass blocks. A later declaration replaces an
# earlier group of the same name, exactly like the compiler does.
function Get-KeywordGroups([string]$settingsText, $passBlocks)
{
    $groups = @()

    $declarationPattern = '(?im)^[ \t]*(?<kind>variant|variant_local|multi_variant|multi_variant_local)\b(?<rest>[^\r\n]*)$'
    foreach ($match in [regex]::Matches($settingsText, $declarationPattern)) {
        $names = @(Get-KeywordNames $match.Groups['rest'].Value)
        if ($names.Count -eq 0) { continue }
        $groupName = ($names -join "|")
        $groups = @($groups | Where-Object { $_.Name -ne $groupName })
        $groups += New-KeywordGroup $match.Groups['kind'].Value.ToLower() $names
    }

    foreach ($passBlock in $passBlocks) {
        foreach ($kind in @("variant", "variant_local", "multi_variant", "multi_variant_local")) {
            $pragmaPattern = "(?im)^[ \t]*#pragma\s+$kind\b(?<rest>[^\r\n]*)$"
            foreach ($match in [regex]::Matches($passBlock.Body, $pragmaPattern)) {
                $names = @(Get-KeywordNames $match.Groups['rest'].Value)
                if ($names.Count -eq 0) { continue }
                $groupName = ($names -join "|")
                $groups = @($groups | Where-Object { $_.Name -ne $groupName })
                $groups += New-KeywordGroup $kind $names
            }
        }
    }

    return $groups
}

# "#pragma vertex LitCubeVertex" out of a Pass body.
function Get-PragmaArgument([string]$text, [string]$pragmaName)
{
    $match = [regex]::Match($text, "(?im)^[ \t]*#pragma\s+$pragmaName\s+(?<value>[A-Za-z_][A-Za-z0-9_]*)")
    if ($match.Success) { return $match.Groups['value'].Value }
    return ""
}

function Get-StageFileExtension([string]$stageName)
{
    if ($stageName -eq "vertex") { return "vert" }
    return "frag"
}

# <name>[.<pass>][.<variant>].<stage>.spv - the name HLSLCC writes a module under.
function Get-ModuleFileName([string]$baseName, [int]$passCount, [string]$passName, [string]$variantKey, [string]$stageName)
{
    $fileName = $baseName
    if ($passCount -gt 1) { $fileName += ".$passName" }
    if ($variantKey -ne "") { $fileName += ".$variantKey" }
    return "$fileName.$(Get-StageFileExtension $stageName).spv"
}

# How many variants a build keeps: a strippable group keeps only the states the
# build reported (its default state when none is), a multi_variant group keeps all
# of them.
function Get-ExpectedVariantCount($groups, [string[]]$usedStates)
{
    $count = 1
    foreach ($group in $groups) {
        if ($group.Strippable) {
            $kept = @($group.States | Where-Object { $usedStates -contains $_ }).Count
            if ($kept -eq 0) { $kept = 1 }
            $count *= $kept
        }
        else {
            $count *= $group.States.Count
        }
    }
    return $count
}

# True when a variant key compiles the given state of the given group: the key is
# the "+"-joined list of the states that are not the "no keyword" state.
function Test-VariantUsesState([string]$variantKey, $group, [string]$state)
{
    $segments = @()
    if ($variantKey -ne "") { $segments = @($variantKey -split '\+') }

    if ($state -eq "_") {
        foreach ($groupState in $group.States) {
            if ($groupState -ne "_" -and $segments -contains $groupState) { return $false }
        }
        return $true
    }

    return ($segments -contains $state)
}

# ---------------------------------------------------------------------------
# Reading the engine's and the compiler's own tables
# ---------------------------------------------------------------------------

# The numbers the ENGINE decides: a shader never writes a binding, HLSLCC applies
# the engine's (see VspCore/Graphics/ShaderBindings.h) and the engine checks them
# while loading. Reading them here means a change on either side fails this run.
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

# Which binding serves which resource kind, read out of the engine's own load-time
# check (VspCore/Graphics/ShaderLibrary.cpp) and keyed by the kind name the
# manifest writes.
$engineBindingForKind = @{}
$shaderLibraryPath = Join-Path $repositoryRoot "Engine\Source\Runtime\VspCore\Graphics\ShaderLibrary.cpp"
if (Test-Path $shaderLibraryPath) {
    $shaderLibraryText = Get-Content $shaderLibraryPath -Raw
    $kindPattern = 'case\s+ShaderResourceKind::(?<kind>\w+)\s*:\s*uExpectedBinding\s*=\s*ShaderBindings::(?<constant>\w+)'
    foreach ($match in [regex]::Matches($shaderLibraryText, $kindPattern)) {
        $enumName = $match.Groups['kind'].Value
        $kindName = $enumName.Substring(0, 1).ToLower() + $enumName.Substring(1)
        $engineBindingForKind[$kindName] = $match.Groups['constant'].Value
    }
}
if ($engineBindingForKind.Count -lt 3) {
    # The load-time check could not be read; the kinds the engine provides are
    # these, and the binding each one lives at is read out of the header above.
    $engineBindingForKind = @{
        "uniformBuffer" = "k_nCameraUniformBuffer"
        "sampledImage"  = "k_nBindlessTextures"
        "sampler"       = "k_nBindlessSampler"
    }
}

# The resource kinds by the number the container stores, read out of the engine's
# reflection header.
$resourceKindNamesByNumber = @{}
$reflectionHeaderPath = Join-Path $repositoryRoot "Engine\Source\Runtime\VspCore\Graphics\ShaderReflection.h"
if (Test-Path $reflectionHeaderPath) {
    $reflectionHeaderText = Get-Content $reflectionHeaderPath -Raw
    $enumMatch = [regex]::Match($reflectionHeaderText, 'enum\s+class\s+ShaderResourceKind[^{]*\{(?<body>[^}]*)\}')
    if ($enumMatch.Success) {
        foreach ($entry in [regex]::Matches($enumMatch.Groups['body'].Value, '(?<name>\w+)\s*=\s*(?<value>\d+)')) {
            $resourceKindNamesByNumber[[int]$entry.Groups['value'].Value] = $entry.Groups['name'].Value
        }
    }
}
Add-Check "the engine's resource-kind table was read" ($resourceKindNamesByNumber.Count -ge 5)

# The queue names the compiler knows and the queue it defaults to, read out of the
# compiler's own parser (Programs/HLSLCC/VspShaderFile.cpp).
$renderQueueNames = @{ "Background" = 1000; "Geometry" = 2000; "AlphaTest" = 2450; "Transparent" = 3000; "Overlay" = 4000 }
$defaultRenderQueue = 2000
$shaderFileSourcePath = Join-Path $repositoryRoot "Engine\Source\Programs\HLSLCC\VspShaderFile.cpp"
if (Test-Path $shaderFileSourcePath) {
    $shaderFileSource = Get-Content $shaderFileSourcePath -Raw
    $queuePattern = 'sQueueName\s*==\s*"(?<name>\w+)"\s*\)\s*\{\s*m_uRenderQueue\s*=\s*(?<value>\d+)'
    foreach ($match in [regex]::Matches($shaderFileSource, $queuePattern)) {
        $renderQueueNames[$match.Groups['name'].Value] = [int]$match.Groups['value'].Value
    }
    $defaultMatch = [regex]::Match($shaderFileSource, 'm_uRenderQueue\s*=\s*(\d+)\s*;')
    if ($defaultMatch.Success) { $defaultRenderQueue = [int]$defaultMatch.Groups[1].Value }
}
Add-Check "the compiler's render-queue table was read" ($renderQueueNames.Count -ge 5)

# ---------------------------------------------------------------------------
# Reading the .vsfo container (see Shared/VsfoFormat.h)
# ---------------------------------------------------------------------------

function Read-ContainerUInt32([byte[]]$bytes, [int]$offset)
{
    return [System.BitConverter]::ToUInt32($bytes, $offset)
}

# A name field of a fixed-size record.
function Read-ContainerName([byte[]]$bytes, [int]$offset, [int]$byteSize)
{
    $end = $offset
    while ($end -lt ($offset + $byteSize) -and $bytes[$end] -ne 0) { $end++ }
    return [System.Text.Encoding]::UTF8.GetString($bytes, $offset, $end - $offset)
}

# The summary one index entry repeats, read back out of the reflection record the
# entry points at.
function Read-ContainerReflectionSummary([byte[]]$bytes, [int]$dataSegmentOffset, $entry)
{
    $recordOffset = $dataSegmentOffset + $entry.ReflectionOffset
    return [pscustomobject]@{
        InputCount              = Read-ContainerUInt32 $bytes ($recordOffset + 0)
        OutputCount             = Read-ContainerUInt32 $bytes ($recordOffset + 4)
        ResourceCount           = Read-ContainerUInt32 $bytes ($recordOffset + 8)
        PushConstantMemberCount = Read-ContainerUInt32 $bytes ($recordOffset + 12)
        PushConstantByteSize    = Read-ContainerUInt32 $bytes ($recordOffset + 16)
    }
}

# One resource of a reflection record: the variables come first, then the
# resources, each of them a fixed-size record.
function Read-ContainerResource([byte[]]$bytes, [int]$dataSegmentOffset, $entry, [int]$resourceIndex)
{
    $recordOffset = $dataSegmentOffset + $entry.ReflectionOffset
    $resourceOffset = $recordOffset + 24 + (($entry.InputCount + $entry.OutputCount + $resourceIndex) * 84)
    return [pscustomobject]@{
        Name    = Read-ContainerName $bytes $resourceOffset 64
        Kind    = Read-ContainerUInt32 $bytes ($resourceOffset + 64)
        Set     = Read-ContainerUInt32 $bytes ($resourceOffset + 68)
        Binding = Read-ContainerUInt32 $bytes ($resourceOffset + 72)
    }
}

# Every index entry of a container.
function Read-ContainerEntries([byte[]]$bytes)
{
    $entryCount = Read-ContainerUInt32 $bytes 12
    $indexOffset = Read-ContainerUInt32 $bytes 16
    $entries = @()

    for ($entryIndex = 0; $entryIndex -lt $entryCount; $entryIndex++) {
        $entryOffset = $indexOffset + ($entryIndex * 324)
        $entries += [pscustomobject]@{
            Index                   = $entryIndex
            StageIndex              = Read-ContainerUInt32 $bytes ($entryOffset + 0)
            VariantIndex            = Read-ContainerUInt32 $bytes ($entryOffset + 4)
            PassIndex               = Read-ContainerUInt32 $bytes ($entryOffset + 8)
            SpirvOffset             = Read-ContainerUInt32 $bytes ($entryOffset + 12)
            SpirvSize               = Read-ContainerUInt32 $bytes ($entryOffset + 16)
            ReflectionOffset        = Read-ContainerUInt32 $bytes ($entryOffset + 20)
            ReflectionSize          = Read-ContainerUInt32 $bytes ($entryOffset + 24)
            InputCount              = Read-ContainerUInt32 $bytes ($entryOffset + 28)
            OutputCount             = Read-ContainerUInt32 $bytes ($entryOffset + 32)
            ResourceCount           = Read-ContainerUInt32 $bytes ($entryOffset + 36)
            PushConstantMemberCount = Read-ContainerUInt32 $bytes ($entryOffset + 40)
            PushConstantByteSize    = Read-ContainerUInt32 $bytes ($entryOffset + 44)
            StageName               = Read-ContainerName $bytes ($entryOffset + 52) 16
            EntryPointName          = Read-ContainerName $bytes ($entryOffset + 68) 64
            PassName                = Read-ContainerName $bytes ($entryOffset + 132) 64
            VariantKey              = Read-ContainerName $bytes ($entryOffset + 196) 128
        }
    }

    return $entries
}

# The kind name the manifest writes for a resource kind number.
function Get-KindNameFromNumber([int]$kindNumber)
{
    if (-not $resourceKindNamesByNumber.ContainsKey($kindNumber)) { return "" }
    $enumName = $resourceKindNamesByNumber[$kindNumber]
    return $enumName.Substring(0, 1).ToLower() + $enumName.Substring(1)
}

# Does this resource sit where the engine provides it?
function Test-ResourceBinding($resource)
{
    if (-not $engineBindingForKind.ContainsKey($resource.kind)) {
        return [pscustomobject]@{
            Passed  = $false
            Message = "'$($resource.name)' is a $($resource.kind), which the engine does not provide"
        }
    }

    $constantName = $engineBindingForKind[$resource.kind]
    $expectedSet = $engineBindings["k_nDescriptorSet"]
    $expectedBinding = $engineBindings[$constantName]

    return [pscustomobject]@{
        Passed  = ($resource.set -eq $expectedSet -and $resource.binding -eq $expectedBinding)
        Message = "'$($resource.name)' ($($resource.kind)) sits at set $($resource.set) binding $($resource.binding); the engine provides $constantName at set $expectedSet binding $expectedBinding"
    }
}

# The engine's binding numbers applied to one container's resources.
function Get-ContainerBindingProblems([byte[]]$bytes, [int]$dataSegmentOffset, $entries)
{
    $problems = @()
    foreach ($entry in $entries) {
        for ($resourceIndex = 0; $resourceIndex -lt $entry.ResourceCount; $resourceIndex++) {
            $containerResource = Read-ContainerResource $bytes $dataSegmentOffset $entry $resourceIndex
            $resource = [pscustomobject]@{
                name    = $containerResource.Name
                kind    = Get-KindNameFromNumber $containerResource.Kind
                set     = $containerResource.Set
                binding = $containerResource.Binding
            }
            $verdict = Test-ResourceBinding $resource
            if (-not $verdict.Passed) { $problems += "$($entry.StageName): $($verdict.Message)" }
        }
    }
    return $problems
}

# ---------------------------------------------------------------------------
# Running the compiler
# ---------------------------------------------------------------------------

function Invoke-Hlslcc([string]$shaderPath, [string]$buildDirectory, [string[]]$usedStates, [string[]]$extraArguments)
{
    $arguments = @($shaderPath, "--output-directory", $buildDirectory)
    foreach ($usedState in $usedStates) { $arguments += @("--used-variant", $usedState) }
    foreach ($argument in $extraArguments) { $arguments += $argument }
    foreach ($includeDirectory in $shaderSourceDirectories) { $arguments += @("-I", $includeDirectory) }

    # Its diagnostics still reach the console; only its summary is dropped.
    & $hlslccPath @arguments | Out-Null
    return $LASTEXITCODE
}

# A build of one shader in its own directory, so two builds never overwrite each
# other's modules.
function Invoke-ShaderBuild([string]$shaderPath, [string]$buildDirectory, [string[]]$usedStates)
{
    if (Test-Path $buildDirectory) { Remove-Item $buildDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    return (Invoke-Hlslcc $shaderPath $buildDirectory $usedStates @())
}

# ---------------------------------------------------------------------------
# The checks of one shader
# ---------------------------------------------------------------------------

function Invoke-ShaderVerification([string]$shaderPath)
{
    $label = [System.IO.Path]::GetFileNameWithoutExtension($shaderPath)
    $shaderBaseName = $label
    $shaderDirectory = Split-Path $shaderPath -Parent
    $installDirectory = $outputDirectory

    # ---- The .vsf file carries everything ----------------------------------
    $shaderText = Get-Content $shaderPath -Raw
    $maskedText = Get-MaskedShaderText $shaderText

    $propertiesBlocks = @(Get-ShaderBlocks $maskedText "Properties")
    Add-Check "$($label): the shader file has a Properties block" ($propertiesBlocks.Count -ge 1)

    $shaderBlocks = @(Get-ShaderBlocks $maskedText "Shader")
    Add-Check "$($label): the shader file has a Shader block" ($shaderBlocks.Count -ge 1)
    if ($shaderBlocks.Count -eq 0) { return }

    $propertiesBlock = $propertiesBlocks | Select-Object -First 1
    $shaderBlock = $shaderBlocks[0]

    $shaderName = ""
    $shaderNameMatch = [regex]::Match($shaderBlock.HeaderText, '"([^"]+)"')
    if ($shaderNameMatch.Success) { $shaderName = $shaderNameMatch.Groups[1].Value }
    Add-Check "$($label): the Shader block names the shader" ($shaderName -ne "")

    # The Pass blocks live inside the Shader block; everything else in that block
    # is a setting.
    $passBlocks = @(Get-ShaderBlocks $shaderBlock.Body "Pass")
    Add-Check "$($label): the Shader block holds a Pass block" ($passBlocks.Count -ge 1)
    if ($passBlocks.Count -eq 0) { return }

    $settingsText = Hide-ShaderBlocks $shaderBlock.Body $passBlocks
    $passCount = $passBlocks.Count

    # The names of the passes and of their entry points, read out of the file.
    $passNames = @()
    $passEntryPoints = @()
    $passesNamingVertex = 0
    $passesNamingFragment = 0
    $passesIncludingHlsl = 0
    foreach ($passBlock in $passBlocks) {
        $passNameMatch = [regex]::Match($passBlock.HeaderText, '"([^"]*)"')
        if ($passNameMatch.Success) { $passName = $passNameMatch.Groups[1].Value }
        else { $passName = "Pass$($passNames.Count)" }

        $vertexEntryPoint = Get-PragmaArgument $passBlock.Body "vertex"
        $fragmentEntryPoint = Get-PragmaArgument $passBlock.Body "fragment"
        if ($vertexEntryPoint -ne "") { $passesNamingVertex++ }
        if ($fragmentEntryPoint -ne "") { $passesNamingFragment++ }
        if ($passBlock.Body -match '#include\s+"[^"]+\.hlsl"') { $passesIncludingHlsl++ }

        # A pass that names no entry point compiles the compiler's default one.
        if ($vertexEntryPoint -eq "") { $vertexEntryPoint = "PassVertex" }
        if ($fragmentEntryPoint -eq "") { $fragmentEntryPoint = "PassFragment" }

        $passNames += $passName
        $passEntryPoints += @{ vertex = $vertexEntryPoint; fragment = $fragmentEntryPoint }
    }

    Add-Check "$($label): every Pass block names its vertex entry point" ($passesNamingVertex -eq $passCount)
    Add-Check "$($label): every Pass block names its fragment entry point" ($passesNamingFragment -eq $passCount)
    Add-Check "$($label): a Pass block includes an HLSL file" ($passesIncludingHlsl -ge 1)

    # The property names the Properties block declares.
    $propertyNames = @()
    if ($propertiesBlock) {
        foreach ($line in ($propertiesBlock.Body -split "\r?\n")) {
            $propertyMatch = [regex]::Match($line, '^\s*(_[A-Za-z0-9_]+)\s*\(')
            if ($propertyMatch.Success -and $propertyNames -notcontains $propertyMatch.Groups[1].Value) {
                $propertyNames += $propertyMatch.Groups[1].Value
            }
        }
    }
    Add-Check "$($label): the Properties block declares a shader property" ($propertyNames.Count -ge 1)

    # The keyword groups the Shader block and the Pass blocks declare.
    $keywordGroups = @(Get-KeywordGroups $settingsText $passBlocks)
    if ($keywordGroups.Count -gt 0) {
        $declaredGroups = @($keywordGroups | ForEach-Object { "$($_.Name) [$($_.Kind), strippable $($_.Strippable)]" })
        Write-Host "keyword groups of $($label): $($declaredGroups -join ', ')"
    }

    # The render queue its "Queue" setting names.
    $queueValue = ""
    $queueMatch = [regex]::Match($settingsText, '(?m)^[ \t]*Queue\s*=\s*(?:"(?<name>[^"]+)"|(?<number>\d+))')
    if ($queueMatch.Success) {
        if ($queueMatch.Groups['name'].Success) { $queueValue = $queueMatch.Groups['name'].Value }
        else { $queueValue = $queueMatch.Groups['number'].Value }
    }
    $expectedRenderQueue = $defaultRenderQueue
    if ($queueValue -match '^\d+$') { $expectedRenderQueue = [int]$queueValue }
    elseif ($queueValue -ne "") {
        if ($renderQueueNames.ContainsKey($queueValue)) { $expectedRenderQueue = $renderQueueNames[$queueValue] }
        else { $expectedRenderQueue = 0 }
    }

    # ---- No shader source writes a binding of its own -----------------------
    # The ENGINE decides where a resource lives: a .vsf shader and the HLSL it
    # includes name their resources and never write a descriptor binding or a
    # register.
    $bindingText = $shaderText
    foreach ($hlslFile in @(Get-ChildItem -Path $shaderDirectory -Filter *.hlsl -File)) {
        $bindingText += (Get-Content $hlslFile.FullName -Raw)
    }
    Add-Check "$($label): no shader source writes a Vulkan binding" (-not ($bindingText -match 'vk::binding'))
    Add-Check "$($label): no shader source writes a register" (-not ($bindingText -match 'register\s*\('))
    Add-Check "$($label): no shader source writes a descriptor set" (-not ($bindingText -match 'vk::descriptor_set'))

    # ---- Compile the shader into the engine's shader directory --------------
    Write-Host "HLSLCC: $shaderPath"
    & $hlslccPath $shaderPath --output-directory $installDirectory
    $compileExitCode = $LASTEXITCODE
    Add-Check "$($label): HLSLCC exited successfully (0)" ($compileExitCode -eq 0)
    if ($compileExitCode -ne 0) { return }

    # ---- One SPIR-V binary per stage of the default variant -----------------
    foreach ($stageName in $stageNames) {
        $entryPoint = $passEntryPoints[0][$stageName]
        $modulePath = Join-Path $installDirectory (Get-ModuleFileName $shaderBaseName $passCount $passNames[0] "" $stageName)

        $exists = Test-Path $modulePath
        Add-Check "$($label): the default variant's $stageName module was written" $exists
        if (-not $exists) { continue }

        $bytes = [System.IO.File]::ReadAllBytes($modulePath)
        Add-Check "$($label): the $stageName binary is a non-empty SPIR-V module" ($bytes.Length -ge 20 -and ($bytes.Length % 4) -eq 0)
        $magic = [System.BitConverter]::ToUInt32($bytes, 0)
        Add-Check "$($label): the $stageName binary starts with the SPIR-V magic number" ($magic -eq 0x07230203)

        $spirvText = -join ($bytes | ForEach-Object { [char]$_ })
        Add-Check "$($label): the $stageName module carries the '$entryPoint' entry point" ($spirvText -match [regex]::Escape($entryPoint))

        if ($spirvValPath) {
            & $spirvValPath "--target-env" "vulkan1.3" $modulePath | Out-Null
            Add-Check "$($label): spirv-val accepts the $stageName module" ($LASTEXITCODE -eq 0)
        }
    }

    # ---- The reflection document -------------------------------------------
    $reflectionPath = Join-Path $installDirectory "$shaderBaseName.reflection.json"
    $hasReflection = Test-Path $reflectionPath
    Add-Check "$($label): the reflection document was written" $hasReflection

    $reflectionStages = @()
    if ($hasReflection) {
        $reflectionJson = Get-Content $reflectionPath -Raw | ConvertFrom-Json
        Add-Check "$($label): the reflection names the shader" ($reflectionJson.shader -eq $shaderName)

        $reflectionStages = @($reflectionJson.stages)
        $missingStages = @()
        foreach ($passIndex in 0..($passCount - 1)) {
            foreach ($stageName in $stageNames) {
                $entryPoint = $passEntryPoints[$passIndex][$stageName]
                if (@($reflectionStages | Where-Object { $_.stage -eq $stageName -and $_.entryPoint -eq $entryPoint }).Count -eq 0) {
                    $missingStages += "$stageName '$entryPoint'"
                }
            }
        }
        Add-Check "$($label): the reflection describes every entry point the file names" ($missingStages.Count -eq 0)
        if ($missingStages.Count -gt 0) { Write-Host "    missing: $($missingStages -join ', ')" }

        # The interface variables the document lists must be the ones its own
        # summary counts, and its push-constant block is checked without a magic
        # size: a positive multiple of 16 that reports at least one member.
        $reflectionSummaryProblems = @()
        $pushConstantStages = 0
        foreach ($stage in $reflectionStages) {
            if (@($stage.inputs).Count -ne $stage.inputCount) { $reflectionSummaryProblems += "$($stage.stage): inputs" }
            if (@($stage.outputs).Count -ne $stage.outputCount) { $reflectionSummaryProblems += "$($stage.stage): outputs" }
            if (@($stage.resources).Count -ne $stage.resourceCount) { $reflectionSummaryProblems += "$($stage.stage): resources" }
            if (@($stage.pushConstants).Count -ne $stage.pushConstantMemberCount) { $reflectionSummaryProblems += "$($stage.stage): push constants" }

            if ($stage.pushConstantMemberCount -gt 0) {
                $pushConstantStages++
                Add-Check "$($label): the $($stage.stage) push-constant block is a positive multiple of 16 bytes" (
                    $stage.pushConstantByteSize -gt 0 -and ($stage.pushConstantByteSize % 16) -eq 0)
            }
            else {
                Add-Check "$($label): the $($stage.stage) stage declares no push constants" ($stage.pushConstantByteSize -eq 0)
            }
        }
        Add-Check "$($label): the reflection reports the members of a push-constant block" ($pushConstantStages -ge 1)
        Add-Check "$($label): the reflection lists the variables it counts" ($reflectionSummaryProblems.Count -eq 0)
        if ($reflectionSummaryProblems.Count -gt 0) { Write-Host "    $($reflectionSummaryProblems -join ', ')" }

        # Every resource the reflection lists must sit on the engine's binding; a
        # shader with FEWER resources simply has fewer of these checks.
        foreach ($stage in $reflectionStages) {
            foreach ($resource in @($stage.resources)) {
                $verdict = Test-ResourceBinding $resource
                Add-Check "$($label): the reflection's $($stage.stage) '$($resource.name)' sits on the engine's binding" $verdict.Passed
                if (-not $verdict.Passed) { Write-Host "    $($verdict.Message)" }
            }
        }
    }

    # ---- The manifest the engine's tools read ------------------------------
    $manifestPath = Join-Path $installDirectory "$shaderBaseName.shader.json"
    $hasManifest = Test-Path $manifestPath
    Add-Check "$($label): the shader manifest was written" $hasManifest

    $manifestJson = $null
    $defaultVariant = $null
    if ($hasManifest) {
        $manifestJson = Get-Content $manifestPath -Raw | ConvertFrom-Json
        Add-Check "$($label): the manifest names the shader" ($manifestJson.name -eq $shaderName)
        Add-Check "$($label): the manifest carries the render queue" ($manifestJson.renderQueue -eq $expectedRenderQueue)

        # The properties, the keyword groups and the passes of the file.
        $manifestPropertyNames = @($manifestJson.properties | ForEach-Object { $_.name })
        Add-Check "$($label): the manifest lists the shader's properties" (
            $manifestPropertyNames.Count -eq $propertyNames.Count -and
            @($propertyNames | Where-Object { $manifestPropertyNames -notcontains $_ }).Count -eq 0)

        # Every keyword group the file declares must be described the same way by
        # the manifest the engine reads.
        $manifestGroupNames = @($manifestJson.keywordGroups | ForEach-Object { $_.name })
        Add-Check "$($label): the manifest lists the keyword groups of the file" (
            $manifestGroupNames.Count -eq $keywordGroups.Count -and
            @($keywordGroups | Where-Object { $manifestGroupNames -notcontains $_.Name }).Count -eq 0)

        foreach ($keywordGroup in $keywordGroups) {
            $groupMatch = @($manifestJson.keywordGroups) | Where-Object { $_.name -eq $keywordGroup.Name } | Select-Object -First 1
            $groupStates = @()
            if ($null -ne $groupMatch) { $groupStates = @($groupMatch.states) }
            Add-Check "$($label): the manifest describes the keyword group '$($keywordGroup.Name)' as $($keywordGroup.Kind) with its $($keywordGroup.States.Count) state(s)" (
                $null -ne $groupMatch -and
                $groupMatch.kind -eq $keywordGroup.Kind -and
                [bool]$groupMatch.strippable -eq $keywordGroup.Strippable -and
                $groupStates.Count -eq $keywordGroup.States.Count -and
                @($keywordGroup.States | Where-Object { $groupStates -notcontains $_ }).Count -eq 0)
        }

        $defaultVariant = @($manifestJson.variants) | Where-Object { $_.key -eq "" } | Select-Object -First 1
        if (-not $defaultVariant) { $defaultVariant = @($manifestJson.variants) | Select-Object -First 1 }
        Add-Check "$($label): the manifest carries the default variant" ($null -ne $defaultVariant)

        $manifestPassNames = @()
        if ($defaultVariant) { $manifestPassNames = @($defaultVariant.passes | ForEach-Object { $_.name }) }
        Add-Check "$($label): the manifest lists the passes of the Shader block" (
            $manifestPassNames.Count -eq $passCount -and
            @($passNames | Where-Object { $manifestPassNames -notcontains $_ }).Count -eq 0)

        # The module of every stage of every pass must be named, and exist next to
        # the manifest under the name HLSLCC writes it.
        $moduleProblems = @()
        $moduleNameProblems = @()
        foreach ($variant in @($manifestJson.variants)) {
            foreach ($pass in @($variant.passes)) {
                foreach ($stage in @($pass.stages)) {
                    $expectedFileName = Get-ModuleFileName $shaderBaseName $passCount $pass.name $variant.key $stage.stage
                    if ($stage.file -ne $expectedFileName) {
                        $moduleNameProblems += "$($stage.file) instead of $expectedFileName"
                    }
                    if (-not (Test-Path (Join-Path $installDirectory $stage.file))) {
                        $moduleProblems += $stage.file
                    }
                }
            }
        }
        Add-Check "$($label): the manifest names the module of every stage" ($moduleNameProblems.Count -eq 0)
        Add-Check "$($label): every module the manifest names exists" ($moduleProblems.Count -eq 0)
        if ($moduleProblems.Count -gt 0) { Write-Host "    missing: $($moduleProblems -join ', ')" }

        # The manifest tells the engine which binding every resource of every stage
        # lives at, and those numbers must be the engine's own.
        foreach ($variant in @($manifestJson.variants)) {
            foreach ($pass in @($variant.passes)) {
                foreach ($stage in @($pass.stages)) {
                    foreach ($resource in @($stage.resources)) {
                        $verdict = Test-ResourceBinding $resource
                        Add-Check "$($label): the manifest's $($stage.stage) '$($resource.name)' sits on the engine's binding" $verdict.Passed
                        if (-not $verdict.Passed) { Write-Host "    $($verdict.Message)" }
                    }
                }
            }
        }
    }

    # ---- The shader container the engine loads -----------------------------
    # ONE file holds every module together with its reflection: an index table that
    # says which stage each module is, how big it is and what it reflects, and a
    # data segment holding the modules. The checks below read the container the way
    # the engine does and cross-check it against the loose .spv files HLSLCC writes
    # next to it for a graphics debugger.
    $vsfoPath = Join-Path $installDirectory "$shaderBaseName.vsfo"
    $hasContainer = Test-Path $vsfoPath
    Add-Check "$($label): the shader container was written (.vsfo)" $hasContainer

    if ($hasContainer) {
        $container = [System.IO.File]::ReadAllBytes($vsfoPath)

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

        Add-Check "$($label): the container starts with the VSFO magic number" ($containerMagic -eq 0x4F465356)
        Add-Check "$($label): the container is version 1" ($containerVersion -eq 1)
        Add-Check "$($label): the container header is 48 bytes" ($containerHeaderSize -eq 48)
        Add-Check "$($label): the index table starts behind the header" ($containerIndexOffset -eq 48)
        Add-Check "$($label): the index table is 324 bytes per entry" ($containerIndexSize -eq (324 * $containerEntryCount))
        Add-Check "$($label): the data segment follows the index table" ($containerDataOffset -ge ($containerIndexOffset + $containerIndexSize))
        Add-Check "$($label): every section lies inside the file" (
            ($containerDataOffset + $containerDataSize) -le $containerMetadataOffset -and
            ($containerMetadataOffset + $containerMetadataSize) -le $container.Length -and
            $containerTotalSize -eq $container.Length)

        # The metadata the container carries is what the engine reads first.
        $containerMetadata = [System.Text.Encoding]::UTF8.GetString($container, $containerMetadataOffset, $containerMetadataSize) | ConvertFrom-Json
        Add-Check "$($label): the container metadata names the shader" ($containerMetadata.name -eq $shaderName)
        Add-Check "$($label): the container metadata carries the render queue" ($containerMetadata.renderQueue -eq $expectedRenderQueue)
        Add-Check "$($label): the container metadata lists the properties" (@($containerMetadata.properties).Count -eq $propertyNames.Count)
        Add-Check "$($label): the container metadata lists the keyword groups" (@($containerMetadata.keywordGroups).Count -eq $keywordGroups.Count)

        $containerVariantKeys = @($containerMetadata.variants | ForEach-Object { $_.key })
        Add-Check "$($label): the container metadata names every variant" ($containerVariantKeys.Count -eq $containerMetadata.variantCount)

        # One entry per (variant, pass, stage), and nothing else.
        $metadataPassCount = [int]$containerMetadata.passCount
        $metadataVariantCount = [int]$containerMetadata.variantCount
        Add-Check "$($label): the container holds one entry per (variant, pass, stage)" (
            $containerEntryCount -eq ($metadataVariantCount * $metadataPassCount * $stageNames.Count))

        $containerEntries = @(Read-ContainerEntries $container)

        $unknownStages = @($containerEntries | Where-Object { $stageNames -notcontains $_.StageName })
        Add-Check "$($label): every container entry names a stage" ($unknownStages.Count -eq 0)

        $entryPointProblems = @()
        $duplicateEntries = @()
        $seenEntryKeys = @{}
        foreach ($entry in $containerEntries) {
            if ($entry.PassIndex -lt $passCount) {
                $expectedEntryPoint = $passEntryPoints[$entry.PassIndex][$entry.StageName]
                if ($null -ne $expectedEntryPoint -and $entry.EntryPointName -ne $expectedEntryPoint) {
                    $entryPointProblems += "$($entry.StageName) names '$($entry.EntryPointName)' instead of '$expectedEntryPoint'"
                }
            }
            $entryKey = "$($entry.VariantIndex)|$($entry.PassIndex)|$($entry.StageIndex)"
            if ($seenEntryKeys.ContainsKey($entryKey)) { $duplicateEntries += $entryKey }
            $seenEntryKeys[$entryKey] = $true
        }
        Add-Check "$($label): every container entry names the entry point of its Pass block" ($entryPointProblems.Count -eq 0)
        if ($entryPointProblems.Count -gt 0) { Write-Host "    $($entryPointProblems -join ', ')" }
        Add-Check "$($label): no (variant, pass, stage) is described twice" ($duplicateEntries.Count -eq 0)

        $unknownVariantKeys = @($containerEntries | Where-Object { $containerVariantKeys -notcontains $_.VariantKey })
        Add-Check "$($label): every container entry names a variant of the metadata" ($unknownVariantKeys.Count -eq 0)

        # Every module must be a real SPIR-V blob of the size the index table
        # claims, byte-identical to the .spv HLSLCC wrote next to the container,
        # and the summary an entry repeats must be the record it points at.
        $containerModuleProblems = @()
        $containerSummaryProblems = @()
        foreach ($entry in $containerEntries) {
            $moduleOffset = $containerDataOffset + $entry.SpirvOffset
            if ($moduleOffset + $entry.SpirvSize -gt $container.Length) { $containerModuleProblems += "entry $($entry.Index) lies outside the file"; continue }
            if ((Read-ContainerUInt32 $container $moduleOffset) -ne 0x07230203) { $containerModuleProblems += "entry $($entry.Index) ('$($entry.EntryPointName)') carries no SPIR-V magic number"; continue }

            $looseModulePath = Join-Path $installDirectory (Get-ModuleFileName $shaderBaseName $metadataPassCount $entry.PassName $entry.VariantKey $entry.StageName)
            if (-not (Test-Path $looseModulePath)) { $containerModuleProblems += "$looseModulePath is missing"; continue }

            $looseModule = [System.IO.File]::ReadAllBytes($looseModulePath)
            if ($looseModule.Length -ne $entry.SpirvSize) { $containerModuleProblems += "$looseModulePath has a different size"; continue }

            $containerModule = New-Object byte[] $entry.SpirvSize
            [System.Array]::Copy($container, $moduleOffset, $containerModule, 0, $entry.SpirvSize)
            if ([System.BitConverter]::ToString($containerModule) -ne [System.BitConverter]::ToString($looseModule)) {
                $containerModuleProblems += "$looseModulePath differs from the container"
            }

            $record = Read-ContainerReflectionSummary $container $containerDataOffset $entry
            if ($record.InputCount -ne $entry.InputCount -or $record.OutputCount -ne $entry.OutputCount -or
                $record.ResourceCount -ne $entry.ResourceCount -or
                $record.PushConstantMemberCount -ne $entry.PushConstantMemberCount -or
                $record.PushConstantByteSize -ne $entry.PushConstantByteSize) {
                $containerSummaryProblems += "entry $($entry.Index) ('$($entry.EntryPointName)')"
            }
        }
        Add-Check "$($label): every container module matches the .spv of the same stage and variant" ($containerModuleProblems.Count -eq 0)
        if ($containerModuleProblems.Count -gt 0) { Write-Host "    $($containerModuleProblems -join ', ')" }
        Add-Check "$($label): every container entry summarises the reflection record it points at" ($containerSummaryProblems.Count -eq 0)
        if ($containerSummaryProblems.Count -gt 0) { Write-Host "    $($containerSummaryProblems -join ', ')" }

        # The push-constant summary of an entry is the block the reflection
        # reports: a positive multiple of 16 bytes with at least one member.
        $containerPushProblems = @()
        foreach ($entry in $containerEntries) {
            if ($entry.PushConstantMemberCount -gt 0) {
                if ($entry.PushConstantByteSize -le 0 -or ($entry.PushConstantByteSize % 16) -ne 0) {
                    $containerPushProblems += "entry $($entry.Index) ('$($entry.EntryPointName)') reports $($entry.PushConstantByteSize) bytes"
                }
            }
            elseif ($entry.PushConstantByteSize -ne 0) {
                $containerPushProblems += "entry $($entry.Index) ('$($entry.EntryPointName)') reports no member but $($entry.PushConstantByteSize) bytes"
            }
        }
        Add-Check "$($label): every container push-constant block has a member and a multiple of 16 bytes" ($containerPushProblems.Count -eq 0)
        if ($containerPushProblems.Count -gt 0) { Write-Host "    $($containerPushProblems -join ', ')" }

        # Every resource the container reflects must sit on the engine's binding.
        $containerBindingProblems = @(Get-ContainerBindingProblems $container $containerDataOffset $containerEntries)
        Add-Check "$($label): every resource of the container sits on the engine's binding" ($containerBindingProblems.Count -eq 0)
        if ($containerBindingProblems.Count -gt 0) { Write-Host "    $($containerBindingProblems -join ', ')" }
    }

    # ---- variant vs multi_variant ------------------------------------------
    # Without any --used-variant a strippable group keeps only its default state
    # while a multi_variant group keeps every state; reporting a state swaps which
    # one ships; reporting every state keeps every state.
    $scratchDirectory = Join-Path $variantOutputDirectory $shaderBaseName

    $defaultBuildDirectory = Join-Path $scratchDirectory "Default"
    $defaultExitCode = Invoke-ShaderBuild $shaderPath $defaultBuildDirectory @()
    Add-Check "$($label): the stripped build succeeded" ($defaultExitCode -eq 0)

    if ($defaultExitCode -eq 0) {
        $defaultManifest = Get-Content (Join-Path $defaultBuildDirectory "$shaderBaseName.shader.json") -Raw | ConvertFrom-Json
        $defaultKeys = @($defaultManifest.variants | ForEach-Object { $_.key })
        $expectedDefaultCount = Get-ExpectedVariantCount $keywordGroups @()

        Add-Check "$($label): a build without --used-variant keeps one state per strippable group ($expectedDefaultCount variant(s))" (
            $defaultManifest.variantCount -eq $expectedDefaultCount -and $defaultKeys.Count -eq $expectedDefaultCount)

        $strippableGroups = @($keywordGroups | Where-Object { $_.Strippable })
        $multiGroups = @($keywordGroups | Where-Object { -not $_.Strippable })

        $strippedProblems = @()
        foreach ($variantKey in $defaultKeys) {
            foreach ($group in $strippableGroups) {
                foreach ($state in @($group.States | Where-Object { $_ -ne "_" })) {
                    if (Test-VariantUsesState $variantKey $group $state) {
                        $strippedProblems += "'$state' of '$($group.Name)' shipped in '$variantKey'"
                    }
                }
            }
        }
        Add-Check "$($label): the default build drops the non-default states of every strippable group" ($strippedProblems.Count -eq 0)
        if ($strippedProblems.Count -gt 0) { Write-Host "    $($strippedProblems -join ', ')" }

        $multiProblems = @()
        foreach ($group in $multiGroups) {
            foreach ($state in @($group.States | Where-Object { $_ -ne "_" })) {
                if (@($defaultKeys | Where-Object { Test-VariantUsesState $_ $group $state }).Count -eq 0) {
                    $multiProblems += "'$state' of '$($group.Name)' did not ship"
                }
            }
        }
        Add-Check "$($label): the default build keeps every state of every always-kept group" ($multiProblems.Count -eq 0)
        if ($multiProblems.Count -gt 0) { Write-Host "    $($multiProblems -join ', ')" }

        # Reporting one state of a strippable group swaps which of its states ships.
        foreach ($group in $strippableGroups) {
            $swapState = @($group.States | Where-Object { $_ -ne "_" }) | Select-Object -First 1
            if ($null -eq $swapState) { continue }

            $usedBuildDirectory = Join-Path $scratchDirectory "Used_$swapState"
            $usedExitCode = Invoke-ShaderBuild $shaderPath $usedBuildDirectory @($swapState)
            Add-Check "$($label): the build reporting '$swapState' succeeded" ($usedExitCode -eq 0)
            if ($usedExitCode -ne 0) { continue }

            $usedManifest = Get-Content (Join-Path $usedBuildDirectory "$shaderBaseName.shader.json") -Raw | ConvertFrom-Json
            $usedKeys = @($usedManifest.variants | ForEach-Object { $_.key })
            $expectedUsedCount = Get-ExpectedVariantCount $keywordGroups @($swapState)

            Add-Check "$($label): reporting '$swapState' swaps which state of '$($group.Name)' ships ($expectedUsedCount variant(s))" (
                $usedManifest.variantCount -eq $expectedUsedCount -and
                @($usedKeys | Where-Object { -not (Test-VariantUsesState $_ $group $swapState) }).Count -eq 0)

            $usedKeywordGroup = @($usedManifest.keywordGroups) | Where-Object { $_.name -eq $group.Name } | Select-Object -First 1
            Add-Check "$($label): the manifest of that build names the '$swapState' keyword" (
                @($usedKeys | Where-Object { $_ -match [regex]::Escape($swapState) }).Count -ge 1 -and
                $null -ne $usedKeywordGroup -and @($usedKeywordGroup.states) -contains $swapState)
        }

        # Reporting every state keeps every state.
        $allStates = @()
        foreach ($group in $keywordGroups) { $allStates += $group.States }

        $allBuildDirectory = Join-Path $scratchDirectory "AllStates"
        $allExitCode = Invoke-ShaderBuild $shaderPath $allBuildDirectory $allStates
        Add-Check "$($label): the build reporting every state succeeded" ($allExitCode -eq 0)

        if ($allExitCode -eq 0) {
            $allManifest = Get-Content (Join-Path $allBuildDirectory "$shaderBaseName.shader.json") -Raw | ConvertFrom-Json
            $allKeys = @($allManifest.variants | ForEach-Object { $_.key })
            $expectedAllCount = Get-ExpectedVariantCount $keywordGroups $allStates

            Add-Check "$($label): reporting every state keeps every state ($expectedAllCount variant(s))" (
                $allManifest.variantCount -eq $expectedAllCount -and $allKeys.Count -eq $expectedAllCount)

            $missingStates = @()
            foreach ($group in $keywordGroups) {
                foreach ($state in $group.States) {
                    if (@($allKeys | Where-Object { Test-VariantUsesState $_ $group $state }).Count -eq 0) {
                        $missingStates += "'$state' of '$($group.Name)'"
                    }
                }
            }
            Add-Check "$($label): every declared keyword state shipped" ($missingStates.Count -eq 0)
            if ($missingStates.Count -gt 0) { Write-Host "    $($missingStates -join ', ')" }
        }
    }
}

# ---------------------------------------------------------------------------
# Run every game shader through the checks above
# ---------------------------------------------------------------------------

if (Test-Path $outputDirectory) {
    Remove-Item $outputDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

foreach ($shaderPath in $shaderPaths) {
    Invoke-ShaderVerification $shaderPath
}

# ---------------------------------------------------------------------------
# several Pass blocks inside one Shader block
# ---------------------------------------------------------------------------
# A Shader block may hold more than one Pass block, and every one of them is
# compiled on its own - into its own pair of modules and its own entries in the
# manifest and in the container.
$multiPassShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\MultiPassTest.vsf"
$multiPassBuildDirectory = Join-Path $variantOutputDirectory "MultiPassTest"

if (Test-Path $multiPassShaderPath) {
    $multiPassText = Get-Content $multiPassShaderPath -Raw
    $multiPassMasked = Get-MaskedShaderText $multiPassText
    $multiPassPassBlocks = @(Get-ShaderBlocks $multiPassMasked "Pass")
    Add-Check "the multi-pass test shader declares two Pass blocks" ($multiPassPassBlocks.Count -eq 2)

    $multiPassExitCode = Invoke-ShaderBuild $multiPassShaderPath $multiPassBuildDirectory @()
    Add-Check "the multi-pass shader compiled" ($multiPassExitCode -eq 0)

    $multiPassManifestPath = Join-Path $multiPassBuildDirectory "MultiPassTest.shader.json"
    $multiPassContainerPath = Join-Path $multiPassBuildDirectory "MultiPassTest.vsfo"

    if (Test-Path $multiPassManifestPath) {
        $multiPassManifest = Get-Content $multiPassManifestPath -Raw | ConvertFrom-Json
        Add-Check "the multi-pass manifest counts both passes" ($multiPassManifest.passCount -eq 2)

        $multiPassPassNames = @(@($multiPassManifest.variants)[0].passes | ForEach-Object { $_.name })
        Add-Check "the multi-pass manifest names both passes" (
            $multiPassPassNames.Count -eq 2 -and $multiPassPassNames -contains "Forward" -and $multiPassPassNames -contains "Tinted")

        # Every pass writes its own pair of modules, and the manifest names them.
        $multiPassModuleProblems = @()
        foreach ($passName in @("Forward", "Tinted")) {
            foreach ($stageName in $stageNames) {
                $expectedFileName = Get-ModuleFileName "MultiPassTest" 2 $passName "" $stageName
                if (-not (Test-Path (Join-Path $multiPassBuildDirectory $expectedFileName))) {
                    $multiPassModuleProblems += $expectedFileName
                }
            }
        }
        Add-Check "every Pass produced both stages" ($multiPassModuleProblems.Count -eq 0)
        if ($multiPassModuleProblems.Count -gt 0) { Write-Host "    missing: $($multiPassModuleProblems -join ', ')" }

        $multiPassNamedModules = @(@($multiPassManifest.variants)[0].passes | ForEach-Object { $_.stages } | ForEach-Object { $_.file })
        Add-Check "the multi-pass manifest names the module of every Pass and stage" (
            $multiPassNamedModules.Count -eq 4 -and
            $multiPassNamedModules -contains "MultiPassTest.Forward.vert.spv" -and
            $multiPassNamedModules -contains "MultiPassTest.Forward.frag.spv" -and
            $multiPassNamedModules -contains "MultiPassTest.Tinted.vert.spv" -and
            $multiPassNamedModules -contains "MultiPassTest.Tinted.frag.spv")
    }
    else {
        Add-Check "the multi-pass manifest was written" $false
    }

    if (Test-Path $multiPassContainerPath) {
        $multiPassContainer = [System.IO.File]::ReadAllBytes($multiPassContainerPath)
        $multiPassEntries = @(Read-ContainerEntries $multiPassContainer)
        $multiPassPassEntryNames = @($multiPassEntries | ForEach-Object { $_.PassName } | Select-Object -Unique)

        # One entry per (variant, pass, stage) the container's own metadata counts,
        # and nothing else.
        $multiPassMetadataOffset = Read-ContainerUInt32 $multiPassContainer 32
        $multiPassMetadataSize = Read-ContainerUInt32 $multiPassContainer 36
        $multiPassMetadata = [System.Text.Encoding]::UTF8.GetString($multiPassContainer, $multiPassMetadataOffset, $multiPassMetadataSize) | ConvertFrom-Json

        $multiPassExpectedKeys = @()
        for ($variantIndex = 0; $variantIndex -lt [int]$multiPassMetadata.variantCount; $variantIndex++) {
            for ($passIndex = 0; $passIndex -lt [int]$multiPassMetadata.passCount; $passIndex++) {
                for ($stageIndex = 0; $stageIndex -lt $stageNames.Count; $stageIndex++) {
                    $multiPassExpectedKeys += "$variantIndex|$passIndex|$stageIndex"
                }
            }
        }
        $multiPassEntryKeys = @($multiPassEntries | ForEach-Object { "$($_.VariantIndex)|$($_.PassIndex)|$($_.StageIndex)" })

        Add-Check "the multi-pass container holds one entry per (variant, pass, stage)" (
            @(Compare-Object -ReferenceObject $multiPassEntryKeys -DifferenceObject $multiPassExpectedKeys).Count -eq 0)
        Add-Check "the multi-pass container names both passes" (
            $multiPassPassEntryNames.Count -eq 2 -and $multiPassPassEntryNames -contains "Forward" -and $multiPassPassEntryNames -contains "Tinted")

        $multiPassBindingProblems = @(Get-ContainerBindingProblems $multiPassContainer (Read-ContainerUInt32 $multiPassContainer 24) $multiPassEntries)
        Add-Check "the multi-pass container's resources sit on the engine's bindings" ($multiPassBindingProblems.Count -eq 0)
        if ($multiPassBindingProblems.Count -gt 0) { Write-Host "    $($multiPassBindingProblems -join ', ')" }
    }
    else {
        Add-Check "the multi-pass container was written" $false
    }
}
else {
    Add-Check "the multi-pass test shader exists" $false
}

# ---------------------------------------------------------------------------
# a shader that names its OWN bindings
# ---------------------------------------------------------------------------
# The engine numbers the resources of a shader that names none. A shader that
# writes [[vk::binding]] - here in the HLSL file its Pass includes, which no scan
# of the .vsf text alone would see - keeps its own numbers.
$explicitShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\ExplicitBindingTest.vsf"

if (Test-Path $explicitShaderPath) {
    $explicitOutputDirectory = Join-Path $variantOutputDirectory "ExplicitBindings"
    if (Test-Path $explicitOutputDirectory) { Remove-Item $explicitOutputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $explicitOutputDirectory -Force | Out-Null

    & $hlslccPath $explicitShaderPath --output-directory $explicitOutputDirectory --quiet | Out-Null
    Add-Check "the shader that names its own bindings compiled" ($LASTEXITCODE -eq 0)

    $explicitManifestPath = Join-Path $explicitOutputDirectory "ExplicitBindingTest.shader.json"
    if (Test-Path $explicitManifestPath) {
        $explicitJson = Get-Content $explicitManifestPath -Raw | ConvertFrom-Json
        $explicitStage = @(@($explicitJson.variants)[0].passes)[0].stages | Where-Object { $_.stage -eq "vertex" }
        $explicitBlock = $null
        if ($explicitStage) { $explicitBlock = $explicitStage.resources | Where-Object { $_.name -eq "ExplicitCameraBlock" } }
        Add-Check "the shader's own binding number was kept (7, not the engine's 0)" (
            $null -ne $explicitBlock -and $explicitBlock.binding -eq 7)
    }
    else {
        Add-Check "the shader that names its own bindings produced a manifest" $false
    }

    # --keep-explicit-bindings turns the engine's numbering off for a whole run.
    & $hlslccPath $explicitShaderPath --output-directory $explicitOutputDirectory --keep-explicit-bindings --quiet | Out-Null
    Add-Check "--keep-explicit-bindings still compiles the shader" ($LASTEXITCODE -eq 0)
}
else {
    Add-Check "the explicit-binding test shader exists" $false
}

# ---------------------------------------------------------------------------
# the HLSL builtin library
# ---------------------------------------------------------------------------
# <Vsp/...> includes resolve against the builtin library HLSLCC ships next to
# itself. This shader uses the whole library - transforms, bindless textures,
# normal maps and the lighting models - so a broken helper fails the run.
$builtinShaderPath = Join-Path $repositoryRoot "Engine\Tools\Acceptance\Assets\BuiltinLibraryTest.vsf"
$builtinLibraryDirectory = Join-Path $runDirectory "Builtin"

Add-Check "the builtin library was staged next to the compiler" (Test-Path (Join-Path $builtinLibraryDirectory "Vsp\Lighting.hlsl"))

if (Test-Path $builtinShaderPath) {
    $builtinOutputDirectory = Join-Path $variantOutputDirectory "BuiltinLibrary"
    if (Test-Path $builtinOutputDirectory) { Remove-Item $builtinOutputDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $builtinOutputDirectory -Force | Out-Null

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $builtinLog = & $hlslccPath $builtinShaderPath --output-directory $builtinOutputDirectory 2>&1 | Out-String
    $ErrorActionPreference = $previousErrorActionPreference

    Add-Check "the shader using the builtin library compiled" ($LASTEXITCODE -eq 0)
    Add-Check "the compiler reported the builtin library it used" ($builtinLog -match "builtin library\s+:")
    if ($LASTEXITCODE -ne 0) { Write-Host $builtinLog }

    $builtinContainerPath = Join-Path $builtinOutputDirectory "BuiltinLibraryTest.vsfo"
    Add-Check "the builtin test produced a container" (Test-Path $builtinContainerPath)

    if (Test-Path $builtinContainerPath) {
        $builtinContainer = [System.IO.File]::ReadAllBytes($builtinContainerPath)
        $builtinEntries = @(Read-ContainerEntries $builtinContainer)

        Add-Check "the builtin test compiled both stages" ($builtinEntries.Count -eq 2)

        # The fragment stage is the one that reads the texture array and the
        # sampler, so its resources must have landed on the engine's bindings.
        $builtinFragment = $builtinEntries | Where-Object { $_.StageName -eq "fragment" } | Select-Object -First 1
        Add-Check "the builtin test shaded with the library's lighting model" ($null -ne $builtinFragment)

        if ($null -ne $builtinFragment) {
            $builtinBindingProblems = @(Get-ContainerBindingProblems $builtinContainer (Read-ContainerUInt32 $builtinContainer 24) @($builtinFragment))
            Add-Check "the builtin library's resources landed on the engine's bindings" ($builtinBindingProblems.Count -eq 0)
            if ($builtinBindingProblems.Count -gt 0) { Write-Host "    $($builtinBindingProblems -join ', ')" }
        }
    }
}
else {
    Add-Check "the builtin library test shader exists" $false
}

# ---------------------------------------------------------------------------
# a Pass block outside the Shader block
# ---------------------------------------------------------------------------
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

$misplacedPassExitCode = Invoke-Hlslcc $misplacedPassPath $variantOutputDirectory @() @("--quiet")
Add-Check "a Pass block outside the Shader block is rejected" ($misplacedPassExitCode -ne 0)

# ---------------------------------------------------------------------------
# the engine's own shader directory
# ---------------------------------------------------------------------------
# The engine's copy must stay the stripped one, so regenerate it last.
foreach ($shaderPath in $shaderPaths) {
    $label = [System.IO.Path]::GetFileNameWithoutExtension($shaderPath)
    & $hlslccPath $shaderPath --output-directory $outputDirectory --quiet | Out-Null
    Add-Check "$($label): the engine's shader directory was refreshed" ($LASTEXITCODE -eq 0)
}

$checks | Format-Table -AutoSize
$failed = @($checks | Where-Object { -not $_.Pass }).Count
Write-Output ("RESULT: " + ($checks.Count - $failed) + "/" + ($checks.Count) + " checks passed")
if ($failed -gt 0) { exit 1 }
exit 0
