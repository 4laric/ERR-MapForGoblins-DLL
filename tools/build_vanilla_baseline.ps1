param(
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$CMake,
    [Parameter(Mandatory=$true)][string]$GameDir,
    [Parameter(Mandatory=$true)][string]$BuildDir
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$game = (Resolve-Path -LiteralPath $GameDir).Path
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$gamePrefix = $game.TrimEnd([char]92, [char]47) + [System.IO.Path]::DirectorySeparatorChar
foreach ($outputRoot in @($repo, $BuildDir)) {
    if ($outputRoot.Equals($game, [System.StringComparison]::OrdinalIgnoreCase) -or
        $outputRoot.StartsWith($gamePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "The source and build directories must be outside the game installation."
    }
}
$env:MFG_PROFILE = "vanilla"
$env:PYTHONUTF8 = "1"

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
# The bundled SoulsFormats probes the current directory for Oodle before
# loading it. Keep this local restricted input ignored, never package it.
$oodle = Join-Path $game "oo2core_6_win64.dll"
if (!(Test-Path -LiteralPath $oodle)) { throw "Missing game Oodle library: $oodle" }
Copy-Item -LiteralPath $oodle -Destination (Join-Path $PSScriptRoot "oo2core_6_win64.dll")
# Preserve other user configuration. Explicitly select vanilla in every stage.
$configFile = Join-Path $PSScriptRoot "config.ini"
if (Test-Path -LiteralPath $configFile) {
    throw "Move existing tools/config.ini aside before this dedicated baseline build."
}
try {
    $configText = "[paths]" + [Environment]::NewLine + "game_dir = " + $game.Replace([char]92, [char]47)
    [System.IO.File]::WriteAllText($configFile, $configText, [System.Text.UTF8Encoding]::new($false))
    Push-Location $repo
    try {
        foreach ($generator in @("generate_logo", "generate_map_icons", "generate_overlay_icons", "generate_i18n")) {
            Invoke-Checked $Python @("tools/$generator.py")
        }
        Invoke-Checked $Python @("tools/baseline_manifest.py", "--game-dir", $game, "--output", "$BuildDir/inputs-before.json")
        Invoke-Checked $Python @("tools/check_aobs.py", "--exe", "$game/eldenring.exe", "--json", "$BuildDir/aobs.json")
        Invoke-Checked $Python @("tools/build_pipeline.py", "--profile", "vanilla", "--force-all")
        Invoke-Checked $Python @("tools/baseline_manifest.py", "--game-dir", $game, "--output", "$BuildDir/inputs-after.json", "--compare-inputs", "$BuildDir/inputs-before.json")
        Invoke-Checked $CMake @("-S", $repo, "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DGENERATED_SUBDIR=generated_vanilla")
        Invoke-Checked $CMake @("--build", $BuildDir, "--config", "Release")
    } finally { Pop-Location }
} finally {
    if (Test-Path -LiteralPath $configFile) { Remove-Item -LiteralPath $configFile }
}
