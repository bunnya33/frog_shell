param(
    [string]$QtRoot = "$PSScriptRoot/../.tools/Qt/6.8.3/msvc2022_64",
    [string]$OpenSSLRoot = "$PSScriptRoot/../.tools/Qt/Tools/OpenSSLv3/Win_x64",
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/..").Path
$qtSdk = (Resolve-Path $QtRoot).Path
$cryptoSdk = (Resolve-Path $OpenSSLRoot).Path
Push-Location $projectRoot
try {
    cmake -S . -B build -A x64 "-DCMAKE_PREFIX_PATH=$qtSdk" "-DOPENSSL_ROOT_DIR=$cryptoSdk"
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build build --config $Configuration --parallel
    if ($LASTEXITCODE) { throw 'Build failed' }
    $env:PATH = "$qtSdk/bin;$cryptoSdk/bin;$env:PATH"
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
} finally { Pop-Location }
