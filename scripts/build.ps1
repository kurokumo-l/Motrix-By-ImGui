[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Debug",
    [switch]$Clean,
    [switch]$Test,
    [switch]$CheckStandards
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build\$Config"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $vsPath) {
        $vsPath = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
    }
    $devShellDll = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Import-Module $devShellDll
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -Arch amd64 | Out-Null

    $vsNinjaDir = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    $vsCmakeDir = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    foreach ($d in @($vsCmakeDir, $vsNinjaDir)) {
        if ((Test-Path $d) -and ($env:Path -notlike "*$d*")) {
            $env:Path = "$env:Path;$d"
        }
    }
}

Write-Host "Configuring CMake ($Config, Ninja)..." -ForegroundColor Cyan
& cmake -S $Root -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$CompileDb = Join-Path $BuildDir "compile_commands.json"
if (Test-Path $CompileDb) {
    Copy-Item -Path $CompileDb -Destination (Join-Path $Root "compile_commands.json") -Force
}

Write-Host "Building Motrix Native ($Config)..." -ForegroundColor Cyan
& cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    Write-Host "Running ctest ($Config)..." -ForegroundColor Cyan
    & ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($CheckStandards) {
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot "check_standards.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
