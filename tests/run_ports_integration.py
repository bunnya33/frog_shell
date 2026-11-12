"""Qt ports lifecycle tests with the shared, isolated tunnel fixture (no real servers)."""
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

binary = Path(sys.argv[1]).resolve()
hidden = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}


def fixture_audit(info, command="stats"):
    with socket.create_connection(("127.0.0.1", info["control"]), timeout=3) as peer:
        peer.sendall(command.encode() + b"\n")
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = peer.recv(65536)
            if not chunk:
                raise RuntimeError("Ports fixture closed its audit connection")
            data.extend(chunk)
        return json.loads(data)


with tempfile.TemporaryDirectory(prefix="rabbitshell-ports-ui-") as temporary:
    directory = Path(temporary)
    ready = directory / "ready.json"
    env = os.environ.copy()
    env["RABBITSHELL_DATA_DIR"] = str(directory / "config")
    env["SSH_AUTH_SOCK"] = str(directory / "no-agent")
    env["QT_QPA_PLATFORM"] = "offscreen"
    fixture = subprocess.Popen([sys.executable, str(Path(__file__).with_name("tunnel_fixture.py")), "--ready", str(ready), "--ui-sftp-rejection"],
                               env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, **hidden)
    try:
        deadline = time.monotonic() + 20
        while not ready.exists():
            if fixture.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("Isolated ports fixture did not start")
            time.sleep(.05)
        env["RABBITSHELL_TUNNEL_FIXTURE"] = str(ready)
        report = directory / "ports-ui-tests.txt"
        result = subprocess.run([str(binary), *sys.argv[2:], "-o", str(report) + ",txt"], env=env,
                                capture_output=True, timeout=180, **hidden)
        sys.stdout.buffer.write(result.stdout + result.stderr)
        content = report.read_bytes() if report.exists() else b""
        sys.stdout.buffer.write(content)
        info = json.loads(ready.read_text())
        audit = {}
        for _ in range(80):
            audit = fixture_audit(info)
            if not audit["active_transports"] and not audit["active_relays"]:
                break
            time.sleep(.05)
        output = os.environ.get("RABBITSHELL_TEST_REPORT_DIR")
        if output:
            destination = Path(output)
            destination.mkdir(parents=True, exist_ok=True)
            if report.exists():
                shutil.copyfile(report, destination / "ports-ui-tests.txt")
            (destination / "ports-ui-audit.json").write_text(json.dumps(audit, indent=2), encoding="utf-8")
            if audit["active_transports"] or audit["active_relays"]:
                (destination / "ports-fixture-stacks.json").write_text(
                    json.dumps(fixture_audit(info, "stacks"), indent=2), encoding="utf-8")
        print("Ports fixture audit:", json.dumps(audit, sort_keys=True))
        assert all(audit[key] == 0 for key in ("exec", "pty", "shell", "active_transports", "active_relays")), audit
        # One ordinary Session intentionally requests unsupported SFTP; tunnels remain independent.
        if not sys.argv[2:] or "loopbackSftpFailureCredentialsAndIndependentLifecycle" in sys.argv[2:]:
            assert audit["sftp"] == 1, audit
        totals = re.search(rb"Totals: (\d+) passed, (\d+) failed, (\d+) skipped", content)
        sys.exit(result.returncode or (0 if totals and totals[2] == b"0" and totals[3] == b"0" else 1))
    finally:
        fixture.terminate()
        try:
            _, fixture_error = fixture.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            fixture.kill()
            _, fixture_error = fixture.communicate(timeout=5)
        if os.environ.get("RABBITSHELL_TEST_REPORT_DIR"):
            (Path(os.environ["RABBITSHELL_TEST_REPORT_DIR"]) / "ports-fixture-stderr.txt").write_bytes(fixture_error or b"")
