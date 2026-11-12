[CmdletBinding()]
param(
    [string]$Version,
    [string]$PortableExe,
    [string]$ExpectedSha256,
    [string]$Compiler,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$definition = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($definition -notmatch 'project\(RabbitShell VERSION ([0-9]+\.[0-9]+\.[0-9]+)\b') { throw 'CMake project version missing' }
$sourceVersion = $Matches[1]
if (!$Version) { $Version = $sourceVersion }
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or $Version -cne $sourceVersion) { throw 'Version must match the CMake project version' }
if (!$PortableExe) { $PortableExe = Join-Path $root "dist/FrogShell-$Version-portable.exe" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'dist' }
if (!(Test-Path -LiteralPath $PortableExe -PathType Leaf)) { throw "Verified portable executable missing: $PortableExe" }
$PortableExe = (Resolve-Path -LiteralPath $PortableExe).Path
$actualHash = (Get-FileHash -LiteralPath $PortableExe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ExpectedSha256 -and ($ExpectedSha256 -notmatch '^[a-fA-F0-9]{64}$' -or $actualHash -cne $ExpectedSha256.ToLowerInvariant())) { throw 'Portable SHA-256 mismatch' }
$inputVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($PortableExe).ProductVersion
if ($inputVersion -ne $Version) { throw "Portable product version $inputVersion does not match $Version" }
if (!$Compiler) {
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($found) { $Compiler = $found.Source }
    else { $Compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
}
if (!(Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Inno Setup ISCC.exe not found; provide -Compiler' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "FrogShell-$Version-setup.exe"
if (Test-Path -LiteralPath $output) { throw "Output already exists; move it aside before rebuilding: $output" }
$icon = (Resolve-Path -LiteralPath (Join-Path $root 'resources/branding/frogshell.ico')).Path
& $Compiler "/DAppVersion=$Version" "/DPortableExe=$PortableExe" "/DOutputDir=$OutputDirectory" "/DAppIcon=$icon" (Join-Path $root 'installer/FrogShell.iss')
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $output -PathType Leaf)) { throw 'Inno Setup compilation failed' }
if ((Get-FileHash -LiteralPath $PortableExe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $actualHash) { throw 'Portable input changed during compilation' }
Write-Host "Installer: $output"
Write-Host "Portable SHA256: $actualHash"
Write-Host "Installer SHA256: $((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant())"
