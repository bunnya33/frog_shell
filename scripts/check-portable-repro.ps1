param(
    [string]$PythonExe = '',
    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Toolset = 'v143,version=14.44.35207'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
if (!$PythonExe) {
    $PythonExe = & py -3 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE) { throw 'Python 3.10+ required.' }
}
$build = Join-Path $root 'build/portable-0.1.12/repro'
$generated = Join-Path $build 'generated'
& $PythonExe "$PSScriptRoot/make-portable-payload.py" --destination $generated
if ($LASTEXITCODE) { throw 'Second payload generation failed.' }
& cmake -S "$root/launcher" -B "$build/cmake" -G $Generator -A x64 -T $Toolset "-DPORTABLE_GENERATED_DIR=$($generated.Replace('\','/'))"
if ($LASTEXITCODE) { throw 'Second launcher configuration failed.' }
& cmake --build "$build/cmake" --config Release --target RabbitShellPortable --parallel
if ($LASTEXITCODE) { throw 'Second compilation failed.' }
$first = Get-FileHash -LiteralPath "$root/dist/RabbitShell-0.1.12-portable.exe" -Algorithm SHA256
$second = Get-FileHash -LiteralPath "$build/cmake/Release/RabbitShell-0.1.12-portable.exe" -Algorithm SHA256
if ($first.Hash -ne $second.Hash) { throw "Executable hashes differ: $($first.Hash) / $($second.Hash)" }
$firstPayload = Get-FileHash -LiteralPath "$root/build/portable-0.1.12/generated/payload.bin" -Algorithm SHA256
$secondPayload = Get-FileHash -LiteralPath "$generated/payload.bin" -Algorithm SHA256
if ($firstPayload.Hash -ne $secondPayload.Hash) { throw 'Payload hashes differ.' }
[ordered]@{
    status = 'PASS: byte-identical independent generated/build directories; no application or launcher executed'
    exeSha256 = $first.Hash
    payloadSha256 = $firstPayload.Hash
    exeSize = (Get-Item -LiteralPath $first.Path).Length
    scope = 'Same source, old package, MSVC/SDK/CMake and Windows compression implementation; not a cross-toolchain guarantee'
} | ConvertTo-Json | Tee-Object -FilePath "$root/test-results/portable-0.1.12/repro-report.json"
