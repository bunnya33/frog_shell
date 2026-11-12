import argparse
import json
from pathlib import Path
import sys

from portable_format import APP_HASH, Compression, PE, parse_archive, sha256, source_inventory


ROOT = Path(__file__).resolve().parent.parent


def static_check(executable, source, report, version="0.1.12", manifest=None):
    pe = PE(executable)
    imports = pe.imports()
    assert pe.machine == 0x8664
    assert int.from_bytes(pe.data[pe.optional + 68:pe.optional + 70], "little") == 2, "Not a GUI subsystem executable"
    assert {name.lower() for name in imports} == {"kernel32.dll"}, imports
    assert pe.directory(13) == (0, 0), "Unexpected delay imports"
    config = pe.offset(pe.directory(10)[0])
    dependent_flags = int.from_bytes(pe.data[config + 78:config + 80], "little")
    assert dependent_flags == 0x800, "Pre-entry dependency search is not System32-only"
    manifest_xml = pe.resource(24, 1, None).decode("utf-8")
    assert 'level="asInvoker"' in manifest_xml and '<longPathAware ' in manifest_xml
    assert pe.resource(14, 1, None), "Missing embedded icon"
    release_version = version
    version = pe.resource(16, 1, None)
    assert release_version.encode("utf-16-le") in version
    payload = pe.resource(10, 101, None)
    entries = parse_archive(payload)
    compressor = Compression()
    old = source_inventory(source)
    assert len(entries) == len(old)
    assert sum(item["size"] for item in entries) == sum(item["size"] for item in old)
    assert len({item["path"].casefold() for item in entries}) == len(entries)
    expected = {item["path"]: item for item in old}
    for entry in entries:
        stored = payload[entry["offset"]:entry["offset"] + entry["stored_size"]]
        unpacked = compressor.transform(stored, entry["size"]) if entry["algorithm"] else stored
        assert len(unpacked) == entry["size"]
        assert sha256(unpacked) == entry["sha256"] == expected[entry["path"]]["sha256"], entry["path"]
    applications = [item for item in entries if item["path"] in ("RabbitShell.exe", "FrogShell.exe")]
    assert len(applications) == 1
    app_hash = applications[0]["sha256"]
    if release_version == "0.1.12":
        assert app_hash == APP_HASH and len(entries) == 62
        assert sum(item["size"] for item in entries) == 34584228
    assert "NOTICE.md" in expected and any(name.startswith("licenses/") for name in expected)
    generated = manifest or ROOT / ("build/portable-" + release_version) / "generated/payload-manifest.json"
    metadata = json.loads(generated.read_text(encoding="utf-8"))
    assert sha256(payload) == metadata["payload_sha256"]
    assert metadata["version"] == release_version
    assert [{k: item[k] for k in ("path", "size", "sha256")} for item in metadata["files"]] == old
    baseline_path = report / "review-baseline.json"
    baseline = json.loads(baseline_path.read_text(encoding="utf-8-sig")) if baseline_path.exists() else []
    unchanged, changes = [], []
    for item in baseline:
        path = ROOT / item["path"]
        if item["path"] == "README.md":
            continue
        if not path.is_file() or path.stat().st_size != item["size"] or sha256(path.read_bytes()).lower() != item["sha256"].lower():
            changes.append(item["path"])
        else:
            unchanged.append(item["path"])
    assert not changes, f"Protected baseline changed: {changes}"
    result = {"phase": "static-package-inspection", "status": "PASS (static only)", "application_or_launcher_executed": False,
              "exe": str(executable), "size": len(pe.data), "sha256": sha256(pe.data),
              "payload_sha256": sha256(payload), "payload_size": len(payload),
              "unpacked_size": sum(item["size"] for item in entries), "file_count": len(entries), "internal_app_sha256": app_hash,
              "imports": imports, "dependent_load_flags": hex(dependent_flags),
              "subsystem": "Windows GUI", "crt": "static /MT; no VC runtime imports",
              "manifest": "asInvoker, longPathAware", "icon": "embedded seven-size ICO",
              "baseline_sha256": sha256(baseline_path.read_bytes()) if baseline_path.exists() else None, "protected_files_unchanged": len(unchanged),
              "runtime_tests": "Not performed by this static check; consult the separate isolated runtime report"}
    report.mkdir(parents=True, exist_ok=True)
    (report / "static-report.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return metadata


def main():
    parser = argparse.ArgumentParser(description="Portable wrapper verification. Default is static only; never executes the application without --run-isolated.")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--static-only", action="store_true")
    action.add_argument("--run-isolated", action="store_true")
    parser.add_argument("--exe", type=Path, default=ROOT / "dist/RabbitShell-0.1.12-portable.exe")
    parser.add_argument("--source", type=Path, default=ROOT / "dist/RabbitShell-0.1.12")
    parser.add_argument("--report", type=Path, default=ROOT / "test-results/portable-0.1.12")
    parser.add_argument("--version", default="0.1.12")
    parser.add_argument("--manifest", type=Path)
    arguments = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows is required")
    metadata = static_check(arguments.exe, arguments.source, arguments.report, arguments.version, arguments.manifest)
    if arguments.run_isolated:
        from portable_isolation import run_suite
        run_suite(arguments.exe, arguments.source, arguments.report, metadata)


if __name__ == "__main__":
    main()
