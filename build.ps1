# Builds the Burnout CRASH! recompilation with Clang + Ninja inside a VS 2022 developer environment.
#
#   .\build.ps1                         # RelWithDebInfo (optimized, with symbols)
#   .\build.ps1 -Config Release
#   .\build.ps1 -Config Debug -Jobs 8
#   .\build.ps1 -Codegen                # force rexglue codegen to run again
#   .\build.ps1 -SdkDir D:\rexglue-sdk\win-amd64
#
# The ReXGlue SDK is looked up in -SdkDir, then $env:REXGLUE_SDK, then third_party\rexglue-sdk\win-amd64.

param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'RelWithDebInfo',
    # Recompiled translation units are huge (up to ~5 MB each); cap parallelism to bound RAM use.
    [int]$Jobs = 10,
    [switch]$Codegen,
    [string]$SdkDir
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot
$Preset = "win-amd64-$($Config.ToLower())"

if (-not $SdkDir) { $SdkDir = $env:REXGLUE_SDK }
if (-not $SdkDir) { $SdkDir = Join-Path $ProjectDir 'third_party\rexglue-sdk\win-amd64' }
if (-not (Test-Path (Join-Path $SdkDir 'bin\rexglue.exe'))) {
    throw "ReXGlue SDK not found at '$SdkDir'. Download rexglue-sdk-0.10.0.24-dev.gbd833a2-win-amd64.zip " +
          "(see README.md) and extract it to third_party\rexglue-sdk, or pass -SdkDir / set REXGLUE_SDK."
}
$SdkDir = (Resolve-Path $SdkDir).Path

# Import the MSVC/Windows SDK environment (headers + libs that clang targets on Windows).
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$env:PATH = "$vsInstaller;$env:PATH"  # vcvars64.bat calls vswhere.exe via PATH
$vswhere = "$vsInstaller\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio with the C++ x64 toolset was not found.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

# Prefer standalone LLVM (ReXGlue needs Clang 20+), then CMake and Ninja.
$env:PATH = "C:\Program Files\LLVM\bin;C:\Program Files\CMake\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"

Set-Location $ProjectDir

# Configure first: CMakeLists.txt verifies the game files before anything is generated.
cmake --preset $Preset "-DCMAKE_PREFIX_PATH=$SdkDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

if ($Codegen) {
    & "$SdkDir\bin\rexglue.exe" codegen burnoutcrash_manifest.toml --ignore-stamp
    if ($LASTEXITCODE -ne 0) { throw "codegen failed ($LASTEXITCODE)" }
}

cmake --build --preset $Preset -- -j $Jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

Write-Host "`nBuilt: $ProjectDir\out\build\$Preset\burnoutcrash.exe"
