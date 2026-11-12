"""Isolated Windows offscreen startup; optional embedded-icon check, no user configuration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("package", type=Path)
parser.add_argument("report", type=Path)
parser.add_argument("--icon-probe", type=Path)
parser.add_argument("--test-runtime", type=Path, help="Qt6Test.dll used only by the optional temporary QTest harness")
args = parser.parse_args()
if args.icon_probe and not args.test_runtime:
    parser.error("--test-runtime is required with --icon-probe")
package, report = args.package.resolve(), args.report.resolve()
report.mkdir(parents=True, exist_ok=True)
hidden = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
build = json.loads((package / "build-info.json").read_text(encoding="utf-8-sig"))
exe = package / build.get("mainExecutable", "RabbitShell.exe")
assert hashlib.sha256(exe.read_bytes()).hexdigest().upper() == build["executableSha256"]
for relative in ("platforms/qoffscreen.dll", "platforms/qwindows.dll", "licenses/SETI-NOTICES.txt",
                 "licenses/VSCODE-LICENSE.txt", "docs/RabbitShell-浮窗与文件图标设计.md", f"docs/{build.get('displayName','RabbitShell')}-{build['version']}验证记录.md"):
    assert (package / relative).stat().st_size > 0, relative
result = {"version": build["version"], "executableSha256": build["executableSha256"],
          "scope": "Windows offscreen, hidden processes, temporary data/CWD, package DLLs and plugins, no SDK PATH or VSCode resources. Optional icon harness alone additionally loads Qt6Test.dll from its temporary directory.",
          "startup": []}
with tempfile.TemporaryDirectory(prefix="rabbitshell-package-") as temporary:
    directory = Path(temporary)
    env = os.environ.copy()
    env["PATH"] = str(package) + os.pathsep + os.pathsep.join(
        [str(Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"), os.environ.get("SystemRoot", "C:/Windows")])
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_PLUGIN_PATH"] = str(package)
    version = subprocess.run([str(exe), "--version"], env=env, cwd=directory, capture_output=True, timeout=10, **hidden)
    assert version.returncode == 0
    result["versionCommand"] = {"exitCode": version.returncode, "output": version.stdout.decode(errors="replace").strip()}
    for name, options in (("manager", []), ("demo", ["--demo"])):
        process = subprocess.Popen([str(exe), *options, "--data-dir", str(directory / name)],
                                   env=env, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **hidden)
        try:
            time.sleep(1.5)
            alive = process.poll() is None
            assert alive, f"{name} exited early: {process.returncode}"
        finally:
            if process.poll() is None:
                process.terminate()
            stdout, stderr = process.communicate(timeout=5)
        result["startup"].append({"mode": name, "aliveAfterMs": 1500, "stoppedBySmokeHarness": True,
                                  "stderr": stderr.decode(errors="replace")[-3000:]})
    # The same production icon provider, launched with only deployed runtime DLLs.
    # Test executable is temporary and is never added to the delivered package.
    if args.icon_probe:
        probe = directory / "icon-probe.exe"
        shutil.copyfile(args.icon_probe.resolve(), probe)
        shutil.copyfile(args.test_runtime.resolve(), directory / "Qt6Test.dll")
        output = report / "package-icon-probe.txt"
        icon = subprocess.run([str(probe), "setiMappingAndRendering", "-o", str(output) + ",txt"],
                              env=env, cwd=directory, capture_output=True, timeout=30, **hidden)
        assert output.exists(), f"Icon harness failed to start: {icon.returncode}; {icon.stderr.decode(errors='replace')}"
        content = output.read_text(encoding="utf-8")
        assert icon.returncode == 0 and re.search(r"Totals: 3 passed, 0 failed, 0 skipped", content), content
        result["embeddedIcons"] = {"passed": 3, "failed": 0, "skipped": 0, "report": output.name}
historical = {
    "0.1.16": "8E2B30F758175939EAB226091A0B463126906FCAC6CD4C07F71F63C9674D19DB",
    "0.1.15": "1C8BD933E1C181033E9EAF51CF8888A75D2FABD67C3F7AFE0D9A7E4DDF30C77B",
    "0.1.14": "14FD2044637D63412D26DE653B4CDD669058D8594F24A46740A568F941B0AD6F",
    "0.1.13": "5BB2305D10AED393917DB1291535B30CBB6E7FE1DE02C169FDD77AB10EAC428B",
    "0.1.8": "724567061FEDB853B7C7795064D130F6A7D670B3F43B1C40DD62125C4B2C7325",
    "0.1.9": "1B3FEF2267454E7CDA572908D840EB44DC6ACFE8E09062B0D23BBA1CE2521721",
    "0.1.10": "3693E48867B05B940E40FFEFE8C2F99C5DD1504AD59A4577C0C900641E017E5C",
    "0.1.11": "FBCD0980043CAAF52C924C6590FC5895D0983975BDA409DA8551E2B9C5A7057B",
    "0.1.12": "190196EA8A8673E8FEA3CFF300A5B845F8D7BE5DBF62C626AD120356DD2E6CCC",
}
result["historicalPackages"] = {}
for version, expected in historical.items():
    old = package.parent / ("RabbitShell-" + version) / "RabbitShell.exe"
    if old.exists():
        digest = hashlib.sha256(old.read_bytes()).hexdigest().upper()
        assert digest == expected
        result["historicalPackages"][version] = {"status": "matched baseline", "sha256": digest}
    else:
        result["historicalPackages"][version] = {"status": "absent; not checked"}
(report / "package-verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
manifest = [{"path": file.relative_to(package).as_posix(), "bytes": file.stat().st_size,
             "sha256": hashlib.sha256(file.read_bytes()).hexdigest().upper()}
            for file in sorted(package.rglob("*")) if file.is_file()]
(report / "package-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"version": result["version"], "startupModes": 2, "embeddedIconChecks": result.get("embeddedIcons", {}).get("passed", 0),
                  "packageFiles": len(manifest), "historicalPackages": result["historicalPackages"]}))
