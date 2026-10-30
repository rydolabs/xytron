# A Windows-native, offline .tar.bz2 round-trip test for the extractor.
$ErrorActionPreference = "Stop"
$SevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($null -ne $SevenZipCommand) {
    $SevenZipPath = $SevenZipCommand.Source
} else {
    $SevenZipPath = Join-Path $env:ProgramFiles "7-Zip\7z.exe"
    if (-not (Test-Path -LiteralPath $SevenZipPath -PathType Leaf)) {
        throw "7-Zip was not found on this Windows runner"
    }
}
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("nativeweb-cef-archive-test-" + [guid]::NewGuid().ToString("N"))
$RootName = "cef_binary_fixture_windows64_minimal"
$SourceRoot = Join-Path $Work "source"
$FixtureRoot = Join-Path $SourceRoot $RootName
$OutputRoot = Join-Path $Work "extracted"
$TarPath = Join-Path $Work "$RootName.tar"
$ArchivePath = "$TarPath.bz2"
try {
    New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $FixtureRoot "CMakeLists.txt") -Value "# CEF fixture" -Encoding Ascii
    & tar.exe -cf $TarPath -C $SourceRoot $RootName
    if ($LASTEXITCODE -ne 0) { throw "Fixture tar creation failed: $LASTEXITCODE" }
    & $SevenZipPath a -y -tbzip2 $ArchivePath $TarPath
    if ($LASTEXITCODE -ne 0) { throw "Fixture bzip2 creation failed: $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot "expand_cef_archive_windows.ps1") -ArchivePath $ArchivePath -ExtractDir $OutputRoot
    if (-not (Test-Path -LiteralPath (Join-Path $OutputRoot "$RootName\CMakeLists.txt"))) {
        throw "Fixture extraction did not preserve expected root layout"
    }
    Write-Host "PASS: Windows 7-Zip .tar.bz2 extraction fixture"
} finally {
    if (Test-Path -LiteralPath $Work) {
        Remove-Item -LiteralPath $Work -Recurse -Force
    }
}
