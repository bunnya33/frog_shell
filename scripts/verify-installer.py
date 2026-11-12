"""Static installer checks; installation is permitted only on GitHub Actions runners."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import winreg

ROOT = Path(__file__).resolve().parent.parent
APP_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{B865FC75-30B0-47B9-8E44-7135B722F042}_is1"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def fixed_versions(path):
    lib = ctypes.WinDLL('version', use_last_error=True)
    lib.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    lib.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    lib.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    lib.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    size = lib.GetFileVersionInfoSizeW(str(path), None)
    require(size > 0, f'Missing version resource: {path}')
    data = ctypes.create_string_buffer(size)
    require(lib.GetFileVersionInfoW(str(path), 0, size, data), 'GetFileVersionInfo failed')
    ptr, length = ctypes.c_void_p(), wintypes.UINT()
    require(lib.VerQueryValueW(data, '\\', ctypes.byref(ptr), ctypes.byref(length)), 'Version query failed')
    values = ctypes.cast(ptr, ctypes.POINTER(wintypes.DWORD * 13)).contents
    require(values[0] == 0xFEEF04BD, 'Invalid version resource')
    def unpack(ms, ls):
        return (ms >> 16, ms & 65535, ls >> 16, ls & 65535)
    return unpack(values[2], values[3]), unpack(values[4], values[5])


def icon_present(exe, ico):
    # Compare every ICO image with an RT_ICON image in the compiled setup.
    raw = ico.read_bytes()
    reserved, kind, count = struct.unpack_from('<HHH', raw)
    require((reserved, kind) == (0, 1) and count > 0, 'Invalid source icon')
    images = []
    for i in range(count):
        size, offset = struct.unpack_from('<II', raw, 6 + i * 16 + 8)
        require(size and offset + size <= len(raw), 'Invalid icon image')
        images.append(raw[offset:offset + size])
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    k.LoadLibraryExW.restype = wintypes.HMODULE
    k.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    k.FindResourceW.restype = wintypes.HANDLE
    k.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    k.LoadResource.restype = wintypes.HANDLE
    k.LockResource.argtypes = [wintypes.HANDLE]
    k.LockResource.restype = ctypes.c_void_p
    k.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    k.SizeofResource.restype = wintypes.DWORD
    k.FreeLibrary.argtypes = [wintypes.HMODULE]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ssize_t)
    k.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, callback_type, ctypes.c_ssize_t]
    module = k.LoadLibraryExW(str(exe), None, 2)
    require(module, 'Could not load setup resources')
    found = []
    @callback_type
    def collect(module, resource_type, name, unused):
        resource = k.FindResourceW(module, name, resource_type)
        if resource:
            size = k.SizeofResource(module, resource)
            pointer = k.LockResource(k.LoadResource(module, resource))
            if pointer and size:
                found.append(ctypes.string_at(pointer, size))
        return True
    try:
        require(k.EnumResourceNamesW(module, ctypes.c_void_p(3), collect, 0), 'No setup icon resources')
    finally:
        k.FreeLibrary(module)
    require(all(image in found for image in images), 'Setup icon does not match project icon')


def existing_registration():
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, APP_KEY, 0, winreg.KEY_READ | view):
                    return True
            except FileNotFoundError:
                pass
    return False


def smoke(setup, portable, version, manifest):
    require(os.environ.get('GITHUB_ACTIONS') == 'true', 'Installation smoke is restricted to GitHub Actions')
    require(not existing_registration(), 'Existing FrogShell uninstall registration; aborting')
    default = Path(os.environ['LOCALAPPDATA']) / 'Programs' / 'FrogShell'
    require(not default.exists(), 'Existing default FrogShell installation; aborting')
    with tempfile.TemporaryDirectory(prefix='frogshell-installer-ci-') as temp:
        root = Path(temp).resolve(strict=True)
        install = root / 'install'
        require(not install.exists() and install.parent == root, 'Unsafe install directory')
        profile = root / 'profile'
        local, roaming, tmp = profile / 'Local', profile / 'Roaming', profile / 'Temp'
        for directory in (local, roaming, tmp):
            directory.mkdir(parents=True)
        sentinels = []
        for directory in (local / 'RabbitShell', roaming / 'RabbitShell', local / 'FrogShell', roaming / 'FrogShell'):
            directory.mkdir()
            marker = directory / 'preserve-test.txt'
            marker.write_text('preserve', encoding='utf-8')
            sentinels.append(marker)
        system = Path(os.environ['SystemRoot']) / 'System32'
        env = os.environ.copy()
        env.update({'PATH': str(system), 'USERPROFILE': str(profile), 'HOME': str(profile),
                    'LOCALAPPDATA': str(local), 'APPDATA': str(roaming), 'TEMP': str(tmp), 'TMP': str(tmp),
                    'RABBITSHELL_DATA_DIR': str(roaming / 'RabbitShell'), 'RABBITSHELL_PORTABLE_NO_DIALOG': '1',
                    'QT_QPA_PLATFORM': 'offscreen'})
        # Setup uses the real runner's per-user registry; /DIR and /NOICONS isolate its files.
        cmd = [str(setup), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', f'/DIR={install}']
        result = subprocess.run(cmd, timeout=180, capture_output=True)
        require(result.returncode == 0, f'Installer exit code {result.returncode}')
        installed = install / 'FrogShell.exe'
        portable_sha256 = digest(portable)
        require(installed.is_file() and digest(installed) == portable_sha256, 'Installed payload differs from portable')
        require(existing_registration(), 'Expected uninstall entry is missing')
        from portable_isolation import Sandbox, OwnedProcess, snapshot
        installed_before = snapshot(install)
        result = subprocess.run([str(installed), '--version'], env=env, cwd=str(install), capture_output=True, timeout=60)
        output = result.stdout.decode(errors='replace').strip()
        require(result.returncode == 0 and f'FrogShell {version}' in output, f'Installed version failed: {output}')
        # Reuse the portable test's Windows job ownership and isolated profile for real Qt startup.
        metadata = json.loads(manifest.read_text(encoding='utf-8'))
        require(metadata['version'] == version, 'Payload manifest version mismatch')
        box = Sandbox(root, 'installed-app', installed, metadata)
        require(installed.resolve(strict=True).parent == install.resolve(strict=True) and installed != box.exe,
                'Startup executable must be the installed launcher, not the sandbox copy')
        startup = []
        for name, options in (('manager', []), ('demo', ['--demo'])):
            require(digest(installed) == portable_sha256 and snapshot(install) == installed_before,
                    f'Installed files changed before {name} startup')
            with OwnedProcess(box, name, box.args(*options), executable=installed) as process:
                deadline = time.monotonic() + 20
                while len(process.pids()) < 2 and time.monotonic() < deadline:
                    process.sample()
                    time.sleep(.025)
                require(len(process.pids()) == 2, f'{name} Qt child missing')
                time.sleep(.6)
                require(len(process.pids()) == 2, f'{name} exited early')
                sample = process.sample()
                require(any(item['pid'] == process.pid and Path(item['image']).resolve() == installed.resolve()
                            for item in sample), f'{name} did not run the installed launcher')
                process.stop_child(73)
                outcome = process.wait()
                require(outcome['exit_code'] == 73, f'{name} launcher exit mismatch')
                startup.append(name)
            box.validate_cache()
            require(digest(installed) == portable_sha256 and snapshot(install) == installed_before,
                    f'Installed files changed during {name} startup')
        app_sentinel = install / 'unknown-user-file.txt'
        app_sentinel.write_text('preserve', encoding='utf-8')
        uninstaller = install / 'unins000.exe'
        require(uninstaller.is_file(), 'Uninstaller missing')
        result = subprocess.run([str(uninstaller), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'], timeout=180, capture_output=True)
        require(result.returncode == 0, f'Uninstaller exit code {result.returncode}')
        deadline = time.monotonic() + 15
        while uninstaller.exists() and time.monotonic() < deadline:
            time.sleep(.1)  # Inno may remove the uninstaller asynchronously.
        require(not installed.exists() and not uninstaller.exists(), 'Owned installation files remain')
        require({p.name for p in install.iterdir()} == {app_sentinel.name}, 'Unexpected installation files remain')
        require(not existing_registration(), 'Uninstall registration remains')
        require(all(p.read_text(encoding='utf-8') == 'preserve' for p in sentinels), 'Profile data was changed')
        require(app_sentinel.read_text(encoding='utf-8') == 'preserve', 'Unknown installation file was changed')
        return {'installed_sha256': portable_sha256, 'version_output': output, 'uninstall_exit': result.returncode,
                'profile_sentinels_preserved': len(sentinels), 'unknown_install_file_preserved': True,
                'offscreen_startup': startup, 'installed_launcher_verified': True,
                'installation_snapshot_unchanged': True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--portable', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--expected-sha256', required=True, help='Pinned portable SHA-256')
    parser.add_argument('--ci-smoke', action='store_true')
    parser.add_argument('--manifest', type=Path, help='Portable payload manifest, required for CI startup checks')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    setup, portable = args.setup.resolve(strict=True), args.portable.resolve(strict=True)
    expected = tuple(int(part) for part in args.version.split('.')) + (0,)
    require(len(expected) == 4 and all(0 <= part <= 65535 for part in expected), 'Invalid version')
    require(digest(portable) == args.expected_sha256.lower(), 'Portable digest mismatch')
    require(all(v == expected for v in fixed_versions(setup)), 'Setup version resource mismatch')
    require(all(v == expected for v in fixed_versions(portable)), 'Portable version resource mismatch')
    icon_present(setup, ROOT / 'resources/branding/frogshell.ico')
    report = {'status': 'PASS', 'version': args.version, 'portable_sha256': digest(portable),
              'setup_sha256': digest(setup), 'setup_icon': 'matches project icon', 'installation': 'not run'}
    if args.ci_smoke:
        require(args.manifest is not None and args.manifest.is_file(), 'CI smoke requires the payload manifest')
        report['installation'] = smoke(setup, portable, args.version, args.manifest)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
