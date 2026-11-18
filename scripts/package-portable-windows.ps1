param(
    [string]$PythonExe = '',
    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Toolset = 'v143,version=14.44.35207',
    [string]$Version = '0.1.53',
    [string]$Source = '',
    [string]$Output = '',
    [string]$Manifest = '',
    [string]$Report = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
$build = Join-Path $root "build/portable-$Version"
$generated = Join-Path $build 'generated'
if (!$Report) { $Report = Join-Path $root "test-results/portable-$Version" }
$report = [System.IO.Path]::GetFullPath($Report)
$productName = if ([version]$Version -ge [version]'0.1.39') { 'FrogShell' } else { 'RabbitShell' }
if (!$Source) { $Source = Join-Path $root "dist/$productName-$Version" }
if (!$Output) { $Output = Join-Path $root "dist/$productName-$Version-portable.exe" }
if ($Version -ne '0.1.12' -and !$Manifest) { throw 'New releases require a pinned manifest' }
if (!$PythonExe) {
    $PythonExe = & py -3 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE) { throw 'Python 3.10+ is required only for packaging and verification.' }
}
$python = (Resolve-Path -LiteralPath $PythonExe).Path
& $python -c 'import sys; assert sys.version_info >= (3,10)'
if ($LASTEXITCODE) { throw 'Python 3.10+ is required.' }
New-Item -ItemType Directory -Force -Path $build,$report | Out-Null
$payloadArgs = @('--destination',$generated,'--source',$Source,'--version',$Version)
if ($Manifest) { $payloadArgs += @('--manifest',$Manifest) }
& $python "$PSScriptRoot/make-portable-payload.py" @payloadArgs
if ($LASTEXITCODE) { throw 'Payload generation failed.' }
& cmake -S "$root/launcher" -B "$build/cmake" -G $Generator -A x64 -T $Toolset "-DPORTABLE_GENERATED_DIR=$($generated.Replace('\','/'))" "-DPORTABLE_VERSION=$Version" "-DPORTABLE_OUTPUT_NAME=$productName-$Version-portable" "-DPORTABLE_PRODUCT_NAME=$productName"
if ($LASTEXITCODE) { throw 'Launcher configuration failed.' }
& cmake --build "$build/cmake" --config Release --target RabbitShellPortable portable-child-probe --parallel
if ($LASTEXITCODE) { throw 'Launcher compilation failed.' }
if ($Version -eq '0.1.12') {
    & $python "$PSScriptRoot/prepare-portable-fixture.py" --generator $Generator --toolset $Toolset
    if ($LASTEXITCODE) { throw 'Packaging-only probe fixture compilation failed.' }
}
$binary = Join-Path $build "cmake/Release/$productName-$Version-portable.exe"
$destination = [System.IO.Path]::GetFullPath($Output)
Copy-Item -LiteralPath $binary -Destination $destination -Force
& $python "$PSScriptRoot/verify-portable.py" --static-only --version $Version --exe $destination --source $Source --report $report --manifest "$generated/payload-manifest.json"
if ($LASTEXITCODE) { throw 'Static package verification failed.' }
$information = [ordered]@{
    phase = 'build and embedded-package inspection; this packaging command does not execute the application'
    version = $Version
    generator = $Generator
    toolset = $Toolset
    cmake = (& cmake --version | Select-Object -First 1)
    python = (& $python --version)
    platform = [System.Environment]::OSVersion.VersionString
    executable = $destination
    size = (Get-Item -LiteralPath $destination).Length
    sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    runtimeTests = 'Not run by this packaging command; see final-report.json for separately recorded runtime validation'
}
$information | ConvertTo-Json | Set-Content -LiteralPath "$report/build-report.json" -Encoding utf8
Write-Output ($information | ConvertTo-Json)
