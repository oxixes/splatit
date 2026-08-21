# Downloads the IANA time zone database that the server needs on Windows.
#
# The date library this server uses does not read time zones from the Windows
# registry, and the Windows build has its download support switched off, so it
# expects the database to already sit in the Downloads folder. Without it the
# account server throws on the first request that touches a user's local time.
#
# Run this once:
#     powershell -ExecutionPolicy Bypass -File fetch-tzdata.ps1

$ErrorActionPreference = "Stop"

$target = Join-Path $env:USERPROFILE "Downloads\tzdata"
$zonesUrl = "https://raw.githubusercontent.com/unicode-org/cldr/main/common/supplemental/windowsZones.xml"
$tzUrl = "https://data.iana.org/time-zones/tzdata-latest.tar.gz"

Write-Host "Installing the time zone database into $target"

if (-not (Test-Path $target)) {
    New-Item -ItemType Directory -Path $target -Force | Out-Null
}

$archive = Join-Path $env:TEMP "tzdata-latest.tar.gz"

Write-Host "Downloading $tzUrl"
Invoke-WebRequest -Uri $tzUrl -OutFile $archive

Write-Host "Extracting"
# tar ships with Windows 10 1803 and later.
tar -xzf $archive -C $target
if ($LASTEXITCODE -ne 0) {
    throw "tar failed to extract $archive"
}
Remove-Item $archive -Force

Write-Host "Downloading windowsZones.xml"
# Windows names its zones differently from IANA, and this file maps between the
# two. Zone lookups fail without it.
Invoke-WebRequest -Uri $zonesUrl -OutFile (Join-Path $target "windowsZones.xml")

$version = Join-Path $target "version"
if (Test-Path $version) {
    Write-Host ("Installed tzdata " + (Get-Content $version -Raw).Trim())
} else {
    Write-Host "Installed tzdata"
}

Write-Host "Done. The server can now resolve time zones."
