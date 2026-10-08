# Verifies and extracts your Burnout CRASH! XBLA package (STFS "LIVE" file) into game\.
#
#   .\tools\extract-package.ps1 -Package <path to the package file>
#
# The package is the file under Content\0000000000000000\58410B5D\000D0000\ on your console's drive.
# Every data block is checked against the package's SHA-1 hash tree first, so a damaged or
# incomplete copy is reported instead of producing broken game files.

param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'game')
)

$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'Stfs.cs')

$stfs = New-Object StfsExtractor (Resolve-Path $Package).Path
try {
    Write-Host 'Verifying package hashes...'
    $ok = $stfs.VerifyAll()
    Write-Host $stfs.Log.ToString()
    if (-not $ok) { throw 'The package is damaged or incomplete. Copy it from your console again.' }

    $stfs.ReadFileTable()
    Write-Host "Extracting $($stfs.Entries.Count) entries to $OutDir ..."
    $stfs.ExtractAll($OutDir)
    Write-Host 'Done.'
} finally {
    $stfs.Close()
}
