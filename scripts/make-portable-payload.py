import argparse
from pathlib import Path
from portable_format import generate


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Generate deterministic, bounded Win32 launcher inputs; never runs the application")
    parser.add_argument("--source", type=Path, default=root / "dist/RabbitShell-0.1.12")
    parser.add_argument("--destination", type=Path, default=root / "build/portable-0.1.12/generated")
    parser.add_argument("--version", default="0.1.12")
    parser.add_argument("--manifest", type=Path, help="Pinned JSON inventory with version and files")
    arguments = parser.parse_args()
    metadata = generate(arguments.source, arguments.destination, version=arguments.version, manifest=arguments.manifest)
    print(f"payload: {metadata['payload_sha256']} ({metadata['payload_size']} bytes)")


if __name__ == "__main__":
    main()
