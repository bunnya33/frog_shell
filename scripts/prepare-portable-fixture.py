import argparse
from pathlib import Path
import shutil
import subprocess

from portable_format import generate


def main():
    parser = argparse.ArgumentParser(description="Compile a separate wrapper fixture for phase2 argument/exit tests; does not run it")
    parser.add_argument("--generator", default="Visual Studio 18 2026")
    parser.add_argument("--toolset", default="v143,version=14.44.35207")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "build/portable-0.1.12"
    source = build / "probe-source"
    source.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build / "cmake/Release/portable-child-probe.exe", source / "RabbitShell.exe")
    (source / "docs").mkdir(exist_ok=True)
    (source / "docs/参数说明.txt").write_text("包装探针，不是 RabbitShell 产品应用。\n", encoding="utf-8")
    (source / "replaceable.dll").write_bytes(b"fixture-only; not loaded\n")
    generated = build / "probe-generated"
    metadata = generate(source, generated, release=False)
    subprocess.run(["cmake", "-S", str(root / "launcher"), "-B", str(build / "probe-cmake"),
                    "-G", arguments.generator, "-A", "x64", "-T", arguments.toolset,
                    "-DPORTABLE_GENERATED_DIR=" + generated.as_posix(),
                    "-DPORTABLE_OUTPUT_NAME=RabbitShell-portable-probe"], check=True)
    subprocess.run(["cmake", "--build", str(build / "probe-cmake"), "--config", "Release",
                    "--target", "RabbitShellPortable", "--parallel"], check=True)
    print("Probe fixture compiled, NOT executed. Payload: " + metadata["payload_sha256"])


if __name__ == "__main__":
    main()
