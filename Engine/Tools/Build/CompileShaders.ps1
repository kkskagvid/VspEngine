<#
.SYNOPSIS
    Compiles every .vsf shader of a game's Shaders folder with HLSLCC.

.DESCRIPTION
    Runs HLSLCC.exe over every .vsf file of -ShaderSourceDirectory and writes the
    SPIR-V modules and the shader manifest into -OutputDirectory, which is where
    the engine loads them from ("Shaders" next to the executable). That folder is
    all the engine needs: it never compiles HLSL itself.

    The step is deliberately forgiving: when HLSLCC.exe has not been built yet it
    prints a note instead of failing, so the solution builds in any project order.
    The acceptance runner compiles the shaders explicitly, so a run always has
    them.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File Engine\Tools\Build\CompileShaders.ps1 -ShaderSourceDirectory Assembly\Shaders -OutputDirectory Engine\Intermediate\Binaries\Debug_x64\Shaders
#>
param(
    # Folder holding the .vsf shaders; defaults to the game Assembly's.
    [string]$ShaderSourceDirectory,

    # Folder the compiled SPIR-V and the manifest go to; defaults to the Debug run
    # directory's Shaders folder.
    [string]$OutputDirectory,

    # HLSLCC.exe to run; defaults to the one next to the output directory's parent.
    [string]$HlslccPath
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")

if (-not $ShaderSourceDirectory) {
    $ShaderSourceDirectory = Join-Path $repositoryRoot "Assembly\Shaders"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot "Engine\Intermediate\Binaries\Debug_x64\Shaders"
}
if (-not $HlslccPath) {
    # The compiler sits next to the run directory the shaders are staged into.
    $HlslccPath = Join-Path (Split-Path $OutputDirectory -Parent) "HLSLCC.exe"
}

if (-not (Test-Path $HlslccPath)) {
    Write-Host "CompileShaders: HLSLCC.exe is not built yet; the shaders are compiled by the acceptance run."
    exit 0
}
if (-not (Test-Path $ShaderSourceDirectory)) {
    Write-Host "CompileShaders: no shader source directory at $ShaderSourceDirectory; nothing to do."
    exit 0
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$shaderFiles = Get-ChildItem $ShaderSourceDirectory -Filter "*.vsf" -Recurse
foreach ($shaderFile in $shaderFiles) {
    & $HlslccPath $shaderFile.FullName --output-directory $OutputDirectory --quiet
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CompileShaders: HLSLCC failed for $($shaderFile.Name) (exit $LASTEXITCODE)."
        exit 1
    }
}

# The shader sources are staged as well: HLSLCC resolves the .hlsl files a .vsf
# includes while it compiles, and keeping them next to the manifest makes the run
# directory self-describing.
foreach ($pattern in @("*.hlsl", "*.vsf")) {
    Get-ChildItem $ShaderSourceDirectory -Filter $pattern -File | ForEach-Object {
        Copy-Item $_.FullName -Destination $OutputDirectory -Force
    }
}

Write-Host "CompileShaders: compiled $($shaderFiles.Count) shader(s) into $OutputDirectory."
exit 0
