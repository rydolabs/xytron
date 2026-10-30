param(
    [string]$CacheDir = "",
    [string]$ExtractDir = ""
)

$ErrorActionPreference = "Stop"

$CefVersion = "144.0.36+g78619fd+chromium-144.0.7559.264"
$CefPlatform = "windows64"
$BaseUrl = "https://cef-builds.spotifycdn.com"
$Archive = "cef_binary_${CefVersion}_${CefPlatform}_minimal.tar.bz2"
$ExpectedSha1 = "ce9d951914e79f179e224f3ba9d55d9363696efe"

if ([string]::IsNullOrWhiteSpace($CacheDir)) {
    $CacheDir = Join-Path (Get-Location) ".cache\cef\windows\archive"
}

if ([string]::IsNullOrWhiteSpace($ExtractDir)) {
    $ExtractDir = Join-Path (Get-Location) ".cache\cef\windows\extracted"
}

New-Item -ItemType Directory -Force -Path $CacheDir | Out-Null
New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null

$ArchivePath = Join-Path $CacheDir $Archive
$Url = "$BaseUrl/$Archive"
$RootName = $Archive.Substring(0, $Archive.Length - ".tar.bz2".Length)
$CefRoot = Join-Path $ExtractDir $RootName

if (-not (Test-Path $ArchivePath)) {
    Write-Host "Downloading $Url"
    curl.exe -L --fail --retry 4 --retry-delay 3 $Url -o "$ArchivePath.partial"
    if ($LASTEXITCODE -ne 0) { throw "CEF archive download failed" }
    Move-Item -Force "$ArchivePath.partial" $ArchivePath
} else {
    Write-Host "Using cached archive: $ArchivePath"
}

$Actual = (Get-FileHash -Path $ArchivePath -Algorithm SHA1).Hash.ToLowerInvariant()

Write-Host "CEF Windows archive SHA1: $Actual"

if ($Actual -ne $ExpectedSha1) {
    Remove-Item -Force $ArchivePath
    throw "CEF SHA1 mismatch. Expected $ExpectedSha1, actual $Actual"
}

if (-not (Test-Path (Join-Path $CefRoot "CMakeLists.txt"))) {
    Write-Host "Extracting CEF to $ExtractDir"
    if (Test-Path $CefRoot) {
        Remove-Item -Recurse -Force $CefRoot
    }

    tar.exe -xjf $ArchivePath -C $ExtractDir
    if ($LASTEXITCODE -ne 0) { throw "CEF extraction failed" }
} else {
    Write-Host "Using extracted CEF: $CefRoot"
}

$Required = @(
    "CMakeLists.txt",
    "README.txt",
    "include\cef_app.h",
    "Release\libcef.dll",
    "Resources\icudtl.dat"
)

foreach ($Relative in $Required) {
    $Path = Join-Path $CefRoot $Relative
    if (-not (Test-Path $Path)) {
        throw "Missing required CEF distribution file: $Relative"
    }
}

Write-Output $CefRoot
