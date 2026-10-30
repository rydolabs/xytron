# Extract an already SHA-verified .tar.bz2 CEF distribution on Windows.
param(
    [Parameter(Mandatory = $true)][string]$ArchivePath,
    [Parameter(Mandatory = $true)][string]$ExtractDir
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "CEF archive does not exist: $ArchivePath"
}
if (-not $ArchivePath.EndsWith(".tar.bz2", [StringComparison]::OrdinalIgnoreCase)) {
    throw "Expected a .tar.bz2 CEF archive: $ArchivePath"
}
$SevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($null -ne $SevenZipCommand) {
    $SevenZipPath = $SevenZipCommand.Source
} else {
    $SevenZipPath = Join-Path $env:ProgramFiles "7-Zip\7z.exe"
    if (-not (Test-Path -LiteralPath $SevenZipPath -PathType Leaf)) {
        throw "7-Zip is required for CEF extraction. Install 7z.exe or place it on PATH."
    }
}
$StagingDir = Join-Path ([System.IO.Path]::GetTempPath()) ("nativeweb-cef-" + [guid]::NewGuid().ToString("N"))
$ArchiveName = [System.IO.Path]::GetFileName($ArchivePath)
$TarName = $ArchiveName.Substring(0, $ArchiveName.Length - ".bz2".Length)
$TarPath = Join-Path $StagingDir $TarName
New-Item -ItemType Directory -Force -Path $StagingDir | Out-Null
New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null
try {
    Write-Host "Expanding CEF bzip2 layer with 7-Zip: $ArchiveName"
    & $SevenZipPath x -y "-o$StagingDir" $ArchivePath
    if ($LASTEXITCODE -ne 0) { throw "CEF bzip2 extraction failed: exit $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $TarPath -PathType Leaf)) {
        throw "CEF bzip2 extraction did not produce expected tar: $TarPath"
    }
    Write-Host "Expanding CEF tar layer with 7-Zip: $TarName"
    & $SevenZipPath x -y "-o$ExtractDir" $TarPath
    if ($LASTEXITCODE -ne 0) { throw "CEF tar extraction failed: exit $LASTEXITCODE" }
} finally {
    # The intermediate tar is large. Do not cache or leak it.
    if (Test-Path -LiteralPath $StagingDir) {
        Remove-Item -LiteralPath $StagingDir -Recurse -Force
    }
}
