"""Exercise both SSH trust paths using temporary loopback keys/configuration only."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from run_tunnel_integration import wait_ready, stop_fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--hostkey-type", choices=("rsa", "ecdsa"), default="ecdsa")
    args, tests = parser.parse_known_args()
    hidden = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with tempfile.TemporaryDirectory(prefix="rabbitshell-trust-") as temporary:
        directory = Path(temporary)
        ready = directory / "ready.json"
        env = os.environ.copy()
        env["RABBITSHELL_DATA_DIR"] = str(directory / "config")
        env["SSH_AUTH_SOCK"] = str(directory / "no-agent")
        env["QT_QPA_PLATFORM"] = "offscreen"
        fixture = subprocess.Popen([sys.executable, str(Path(__file__).with_name("tunnel_fixture.py")),
                                    "--ready", str(ready), "--ui-sftp-rejection",
                                    "--hostkey-type", args.hostkey_type], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, **hidden)
        try:
            wait_ready(fixture, ready)
            info = json.loads(ready.read_text())
            env["RABBITSHELL_TUNNEL_FIXTURE"] = str(ready)
            report = directory / "sshtrust-tests.txt"
            result = subprocess.run([str(args.binary.resolve()), *tests, "-o", str(report) + ",txt"],
                                    env=env, capture_output=True, timeout=180, **hidden)
            content = report.read_bytes() if report.exists() else b""
            sys.stdout.buffer.write(result.stdout + result.stderr + content)
            audit = {}
            for _ in range(80):
                with socket.create_connection(("127.0.0.1", info["control"]), timeout=3) as peer:
                    peer.sendall(b"stats\n")
                    data = bytearray()
                    while not data.endswith(b"\n"):
                        chunk = peer.recv(65536)
                        if not chunk:
                            raise RuntimeError("Trust fixture audit connection closed")
                        data.extend(chunk)
                    audit = json.loads(data)
                if not audit["active_transports"] and not audit["active_relays"]:
                    break
                time.sleep(.05)
            output = os.environ.get("RABBITSHELL_TEST_REPORT_DIR")
            if output:
                destination = Path(output)
                destination.mkdir(parents=True, exist_ok=True)
                if report.exists():
                    shutil.copyfile(report, destination / "sshtrust-tests.txt")
                (destination / "sshtrust-audit.json").write_text(json.dumps(audit, indent=2), encoding="utf-8")
            print("Trust fixture audit:", json.dumps(audit, sort_keys=True))
            assert all(audit[name] == 0 for name in ("exec", "pty", "shell", "direct", "active_transports", "active_relays")), audit
            totals = re.search(rb"Totals: (\d+) passed, (\d+) failed, (\d+) skipped", content)
            return result.returncode or (0 if totals and totals[2] == b"0" and totals[3] == b"0" else 1)
        finally:
            stop_fixture(fixture)


if __name__ == "__main__":
    sys.exit(main())
