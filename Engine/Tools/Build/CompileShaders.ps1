<#
.SYNOPSIS
    Compiles every .vsf shader of the engine with HLSLCC.

.DESCRIPTION
    Runs HLSLCC.exe over every shader file under -ShaderSourceDirectory and writes
    the SPIR-V modules and the shader manifest into -OutputDirectory, which is
    where the engine loads them from ("Shaders" next to the executable).

    The step is deliberately forgiving: when HLSLCC.exe has not been built yet it
    prints a note instead of failing, so the solution builds in any project order.
    The acceptance runner compiles the shaders explicitly, so a run always has
    them.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Build\CompileShaders.ps1 -SolutionDir C:\repo\ -Configuration Debug -Platform x64
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$SolutionDir,
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$ShaderSourceDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"

$runDirectory = Join-Path $SolutionDir "Engine\Intermediate\Binaries\${Configuration}_${Platform}"
$hlslccPath = Join-Path $runDirectory "HLSLCC.exe"

if (-not $ShaderSourceDirectory) {
    $ShaderSourceDirectory = Join-Path $SolutionDir "Engine\Source\Runtime\VspCore\Graphics\Shaders"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $runDirectory "Shaders"
}

if (-not (Test-Path $hlslccPath)) {
    Write-Host "CompileShaders: HLSLCC.exe is not built yet; shaders will be compiled by the acceptance run."
    exit 0
}
if (-not (Test-Path $ShaderSourceDirectory)) {
    Write-Host "CompileShaders: no shader source directory at $ShaderSourceDirectory; nothing to do."
    exit 0
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$shaderFiles = Get-ChildItem $ShaderSourceDirectory -Filter "*.vsf" -Recurse
foreach ($shaderFile in $shaderFiles) {
    & $hlslccPath $shaderFile.FullName --output-directory $OutputDirectory --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CompileShaders: HLSLCC failed for $($shaderFile.Name) (exit $LASTEXITCODE)."
        exit 1
    }
}

# The shader sources are staged as well: HLSLCC resolves the .hlsl files a .vsf
# includes while it compiles, and keeping the sources next to the manifest makes
# the run directory self-describing.
foreach ($pattern in @("*.hlsl", "*.vsf")) {
    Get-ChildItem $ShaderSourceDirectory -Filter $pattern -File | ForEach-Object {
        Copy-Item $_.FullName -Destination $OutputDirectory -Force
    }
}

Write-Host "CompileShaders: compiled $($shaderFiles.Count) shader(s) into $OutputDirectory."
exit 0
