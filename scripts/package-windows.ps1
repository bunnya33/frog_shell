param(
    [string]$QtRoot = "$PSScriptRoot/../.tools/Qt/6.8.3/msvc2022_64",
    [string]$OpenSSLRoot = "$PSScriptRoot/../.tools/Qt/Tools/OpenSSLv3/Win_x64",
    [string]$Version = '0.1.53',
    [string]$BuildRoot = "$PSScriptRoot/../build",
    [string]$Destination = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/..").Path
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
$projectDefinition = [System.IO.File]::ReadAllText("$projectRoot/CMakeLists.txt")
if ($projectDefinition -notmatch 'project\(RabbitShell VERSION ([0-9.]+)' -or $Matches[1] -ne $Version) { throw 'Requested version does not match the project version' }
if (!$Destination) { $Destination = "$projectRoot/dist/FrogShell-$Version" }
$qtSdk = (Resolve-Path $QtRoot).Path
$cryptoSdk = (Resolve-Path $OpenSSLRoot).Path
$destination = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force $destination | Out-Null
$buildDirectory = (Resolve-Path -LiteralPath $BuildRoot).Path
Copy-Item -LiteralPath "$buildDirectory/Release/FrogShell.exe" -Destination $destination
& "$qtSdk/bin/windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler --dir $destination "$destination/FrogShell.exe"
if ($LASTEXITCODE) { throw 'Qt deployment failed' }
if (!(Test-Path -LiteralPath "$destination/Qt6Core5Compat.dll")) { throw 'Qt Core5Compat runtime missing; install qt5compat in the packaging Qt SDK.' }
# Retain the platform plugin used for isolated package smoke checks without a visible desktop.
Copy-Item -LiteralPath "$qtSdk/plugins/platforms/qoffscreen.dll" -Destination "$destination/platforms"
Copy-Item -LiteralPath "$cryptoSdk/bin/libcrypto-3-x64.dll" -Destination $destination
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = & $vswhere -latest -products '*' -property installationPath
    $redistRoot = Join-Path $vsRoot 'VC/Redist/MSVC'
    $runtimeFolder = Get-ChildItem -LiteralPath $redistRoot -Directory | ForEach-Object {
        $candidate = Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath $candidate) { Get-Item -LiteralPath $candidate }
    } | Sort-Object FullName -Descending | Select-Object -First 1
    if ($runtimeFolder) { Get-ChildItem -LiteralPath $runtimeFolder.FullName -Filter '*.dll' | Copy-Item -Destination $destination }
    else { throw 'MSVC x64 runtime directory not found; install the C++ redistributable build component.' }
}
else { throw 'vswhere not found; MSVC runtime deployment must be completed before packaging.' }
Copy-Item -LiteralPath "$projectRoot/third_party/licenses" -Destination $destination -Recurse -Force
Copy-Item -LiteralPath "$projectRoot/README.md","$projectRoot/third_party/NOTICE.md" -Destination $destination
New-Item -ItemType Directory -Force "$destination/docs" | Out-Null
Copy-Item -LiteralPath "$projectRoot/third_party/patches/scintilla-5.5.7.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/editor-scintilla-migration.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-Qt实现记录.md","$projectRoot/docs/RabbitShell-Qt窗口规范落实.md","$projectRoot/docs/RabbitShell-保存密码设计.md","$projectRoot/docs/RabbitShell-保存密码验证.md","$projectRoot/docs/RabbitShell-编辑分组与面板设计.md","$projectRoot/docs/RabbitShell-外观主题规范.md","$projectRoot/docs/RabbitShell-编辑分组验证.md","$projectRoot/docs/RabbitShell-交互设计检查清单.md" -Destination "$destination/docs"
$projectDefinition = [System.IO.File]::ReadAllText("$projectRoot/CMakeLists.txt")
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-终端光标与复制验证.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-进程刷新与卡片验证.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.6验证与内存记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/qt-desktop-guidelines.zh-CN.md","$projectRoot/docs/RabbitShell-0.1.7验证记录.md","$projectRoot/docs/RabbitShell-0.1.8验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-浮窗与文件图标设计.md","$projectRoot/docs/RabbitShell-0.1.9验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.10验证记录.md","$projectRoot/docs/RabbitShell-SSH隧道实现方案.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.11验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.12验证记录.md" -Destination "$destination/docs"
New-Item -ItemType Directory -Force "$destination/scripts" | Out-Null
Copy-Item -LiteralPath "$projectRoot/scripts/package-windows.ps1","$projectRoot/scripts/verify-package.py" -Destination "$destination/scripts"
Copy-Item -LiteralPath "$projectRoot/resources/seti/source.json" -Destination "$destination/docs/seti-source.json"
Copy-Item -LiteralPath "$projectRoot/resources/seti/theme-package.json" -Destination "$destination/docs/seti-package.json"
if ($projectDefinition -notmatch 'project\(RabbitShell VERSION ([0-9.]+)') { throw 'Project version not found' }
$packageVersion = $Matches[1]
$buildInfo = [ordered]@{
    version = $packageVersion
    packagedAtUtc = [DateTime]::UtcNow.ToString('o')
    platform = 'Windows x64'
    qt = '6.8.3'
    displayName = 'FrogShell'
    mainExecutable = 'FrogShell.exe'
    executableSha256 = (Get-FileHash -LiteralPath "$destination/FrogShell.exe" -Algorithm SHA256).Hash
}
$buildInfo | ConvertTo-Json | Set-Content -LiteralPath "$destination/build-info.json" -Encoding utf8
Write-Output "Runnable directory: $destination"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-systemd服务面板设计.md","$projectRoot/docs/RabbitShell-systemd服务实现记录.md","$projectRoot/docs/RabbitShell-0.1.13验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.14验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.15验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/resources/fonts/source.json" -Destination "$destination/docs/cascadia-code-source.json"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.16验证记录.md","$projectRoot/docs/RabbitShell-字体设置.md","$projectRoot/docs/RabbitShell-0.1.16终端重排修复方案.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/third_party/patches/libvterm-0.3.3.md" -Destination "$destination/docs/libvterm-0.3.3-patch.md"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.17验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.18验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.19验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.20验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.21验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.22验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.23验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-目录收藏.md","$projectRoot/docs/RabbitShell-0.1.24验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-右键菜单.md","$projectRoot/docs/RabbitShell-圆角边框.md","$projectRoot/docs/RabbitShell-0.1.25验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-终端清屏与切屏.md","$projectRoot/docs/RabbitShell-0.1.26验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.27资源操作设计.md","$projectRoot/docs/RabbitShell-0.1.27验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.28诊断说明.md","$projectRoot/docs/RabbitShell-0.1.28验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.29验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.30验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.31验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.32验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.33验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.34验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-应用锁与收藏终端组.md","$projectRoot/docs/RabbitShell-0.1.35验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-系统查询与应用菜单.md","$projectRoot/docs/RabbitShell-0.1.36验证记录.md" -Destination "$destination/docs"

Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.37验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/RabbitShell-0.1.38验证记录.md","$projectRoot/docs/0.1.38-implementation.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.39验证记录.md","$projectRoot/docs/0.1.39-implementation.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/resources/branding/source-metadata.json" -Destination "$destination/docs/frogshell-branding.json"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.40验证记录.md","$projectRoot/docs/0.1.40-implementation.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.41验证记录.md","$projectRoot/docs/0.1.41-implementation.zh-CN.md","$projectRoot/docs/0.1.41-ports-implementation.zh-CN.md","$projectRoot/docs/0.1.41-clock-integration.zh-CN.md","$projectRoot/docs/0.1.41-download-diagnostics.zh-CN.md","$projectRoot/docs/worker-syntax-0.1.41.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.42验证记录.md","$projectRoot/docs/FrogShell-0.1.43验证记录.md","$projectRoot/docs/0.1.42-about-update.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/0.1.44-clock.zh-CN.md","$projectRoot/docs/0.1.44-update.zh-CN.md","$projectRoot/docs/0.1.44-layout.zh-CN.md","$projectRoot/docs/FrogShell-0.1.44验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.45验证记录.md","$projectRoot/docs/0.1.45-update.zh-CN.md","$projectRoot/docs/0.1.45-installer.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.46验证记录.md","$projectRoot/docs/0.1.46-update.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.47验证记录.md","$projectRoot/docs/0.1.47-update.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.51验证记录.md","$projectRoot/docs/0.1.51-update.zh-CN.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.52验证记录.md" -Destination "$destination/docs"
Copy-Item -LiteralPath "$projectRoot/docs/FrogShell-0.1.53验证记录.md" -Destination "$destination/docs"
