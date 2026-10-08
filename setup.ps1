# One-step setup: installs missing build tools, downloads the ReXGlue SDK, extracts your game
# package into game\ and builds.
#
#   powershell -ExecutionPolicy Bypass -File .\setup.ps1 -Package <path to your package file>
#
# Steps that are already done are skipped, so it is safe to re-run (e.g. without -Package once game\
# exists). Tools are installed with winget after asking; -Yes skips the question.

param(
    [string]$Package,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'RelWithDebInfo',
    [int]$Jobs = 10,
    [switch]$Yes,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$ProjectDir = $PSScriptRoot

$SdkVersion = '0.10.0.24-dev.gbd833a2'
$SdkUrl = 'https://github.com/rexglue/rexglue-sdk/releases/download/nightly-20261002-bd833a2a/' +
          "rexglue-sdk-$SdkVersion-win-amd64.zip"
$SdkSha256 = '1e19d7ec9be0d9d7e1a0088a34a33b068d4dea520181e81318528ef3b37627e7'
$GameFiles = @{
    'DEFAULT.XEX'       = 'f2389f9b8655e6432a78db8a4adc4cf524e5c56a6a92d013b6f0fb0452d4b425'
    'DLL\CRASH.DLL.XEX' = 'bf66a2d38cf9a79dfe20558d289edb96b9f2346bf344613946a4ada897298338'
}

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }

# --- 1. Build tools -------------------------------------------------------------------------------

function Get-ClangMajor {
    $clang = 'C:\Program Files\LLVM\bin\clang.exe'
    if (-not (Test-Path $clang)) { return 0 }
    if ((& $clang --version | Select-Object -First 1) -match 'clang version (\d+)') { return [int]$Matches[1] }
    return 0
}

function Test-VsCpp {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    return [bool]$path -and (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include")
}

function Test-Tool($name, $knownPath) {
    if (Test-Path $knownPath) { return $true }
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

Step 'Checking build tools'
$missing = @()
if ((Get-ClangMajor) -lt 20) {
    $missing += @{ Name = 'LLVM/Clang 20+'; Args = @('install', '-e', '--id', 'LLVM.LLVM') }
}
if (-not (Test-Tool 'cmake' 'C:\Program Files\CMake\bin\cmake.exe')) {
    $missing += @{ Name = 'CMake'; Args = @('install', '-e', '--id', 'Kitware.CMake') }
}
if (-not (Test-Tool 'ninja' "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ninja.exe")) {
    $missing += @{ Name = 'Ninja'; Args = @('install', '-e', '--id', 'Ninja-build.Ninja') }
}
if (-not (Test-VsCpp)) {
    $missing += @{ Name = 'Visual Studio 2022 Build Tools (C++ workload, ~3-4 GB)'; Args = @(
        'install', '-e', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '--override',
        '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') }
}

if ($missing.Count -eq 0) {
    Write-Host 'All build tools are installed.'
} else {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "Missing: $(($missing | ForEach-Object Name) -join ', '). Install them (see README.md) or install winget, then re-run."
    }
    Write-Host 'These will be installed with winget (this accepts their licenses; admin prompts may appear):'
    $missing | ForEach-Object { Write-Host "  - $($_.Name)" }
    if (-not $Yes -and (Read-Host 'Continue? [y/N]') -notmatch '^[yY]') { throw 'Cancelled.' }
    foreach ($tool in $missing) {
        Write-Host "Installing $($tool.Name)..."
        & winget @($tool.Args + @('--source', 'winget', '--accept-package-agreements', '--accept-source-agreements'))
        if ($LASTEXITCODE -ne 0) { throw "winget failed to install $($tool.Name) (exit $LASTEXITCODE)." }
    }
    if ((Get-ClangMajor) -lt 20) { throw 'Clang 20+ is still not available in C:\Program Files\LLVM.' }
    if (-not (Test-VsCpp)) { throw 'Visual Studio C++ tools are still not detected; finish the installer and re-run.' }
}

# --- 2. ReXGlue SDK -------------------------------------------------------------------------------

Step "ReXGlue SDK $SdkVersion"
$sdkRoot = Join-Path $ProjectDir 'third_party\rexglue-sdk'
if (Test-Path (Join-Path $sdkRoot 'win-amd64\bin\rexglue.exe')) {
    Write-Host 'Already present.'
} else {
    New-Item -ItemType Directory -Force $sdkRoot | Out-Null
    $zip = Join-Path $ProjectDir "third_party\rexglue-sdk-$SdkVersion-win-amd64.zip"
    if (-not (Test-Path $zip) -or (Get-FileHash -Algorithm SHA256 $zip).Hash -ne $SdkSha256) {
        Write-Host "Downloading $SdkUrl"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $SdkUrl -OutFile $zip -UseBasicParsing
    }
    if ((Get-FileHash -Algorithm SHA256 $zip).Hash -ne $SdkSha256) {
        throw "The downloaded SDK does not match the expected SHA-256 ($SdkSha256). Delete $zip and retry."
    }
    Expand-Archive -Path $zip -DestinationPath $sdkRoot -Force
    Write-Host 'Installed.'
}

# --- 3. Game files --------------------------------------------------------------------------------

Step 'Game files'
$gameDir = Join-Path $ProjectDir 'game'
$haveGame = (Test-Path (Join-Path $gameDir 'DEFAULT.XEX')) -and (Test-Path (Join-Path $gameDir 'DLL\CRASH.DLL.XEX'))
if (-not $haveGame) {
    if (-not $Package) {
        throw "No game files in game\. Re-run with -Package <path to your Burnout CRASH! package file> " +
              "(Content\0000000000000000\58410B5D\000D0000\ on your console's drive)."
    }
    & (Join-Path $ProjectDir 'tools\extract-package.ps1') -Package $Package -OutDir $gameDir
}
foreach ($file in $GameFiles.Keys) {
    if ((Get-FileHash -Algorithm SHA256 (Join-Path $gameDir $file)).Hash -ne $GameFiles[$file]) {
        throw "game\$file is not the supported version of the game (see README.md, 'Supported version')."
    }
}
Write-Host 'Supported version found.'

# --- 4. Build -------------------------------------------------------------------------------------

Step "Building ($Config) - the first build takes several minutes"
& (Join-Path $ProjectDir 'build.ps1') -Config $Config -Jobs $Jobs -SdkDir (Join-Path $sdkRoot 'win-amd64')

$exe = Join-Path $ProjectDir "out\build\win-amd64-$($Config.ToLower())\burnoutcrash.exe"
Step "Done: $exe"
if ($Run) { Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) }
