"""Run tunnel QTests with fresh loopback fixture and temporary configuration."""
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def wait_ready(fixture, ready, timeout=20):
    deadline = time.monotonic() + timeout
    while not ready.exists():
        if fixture.poll() is not None:
            _, error = fixture.communicate(timeout=1)
            raise RuntimeError("Isolated tunnel fixture failed: " + (error or b"").decode(errors="replace"))
        if time.monotonic() >= deadline:
            # Reading a live child's stderr waits for EOF indefinitely. Cleanup
            # owns termination and bounded collection of the child's pipes.
            raise TimeoutError("Isolated tunnel fixture startup timed out")
        time.sleep(0.05)


def stop_fixture(fixture):
    if fixture.poll() is None:
        fixture.terminate()
    try:
        fixture.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        fixture.kill()
        fixture.communicate(timeout=5)


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    hidden = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with tempfile.TemporaryDirectory(prefix="rabbitshell-tunnel-") as directory:
        directory = pathlib.Path(directory)
        ready = directory / "ready.json"
        env = os.environ.copy()
        # Always override any inherited real configuration and agent endpoint.
        env["RABBITSHELL_DATA_DIR"] = str(directory / "config")
        env["SSH_AUTH_SOCK"] = str(directory / "no-agent")
        fixture = subprocess.Popen([sys.executable, str(pathlib.Path(__file__).with_name("tunnel_fixture.py")),
                                    "--ready", str(ready)], env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, **hidden)
        try:
            wait_ready(fixture, ready)
            env["RABBITSHELL_TUNNEL_FIXTURE"] = str(ready)
            report = directory / "tunnel-tests.txt"
            result = subprocess.run([str(binary), *sys.argv[2:], "-o", str(report) + ",txt"],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=240, **hidden)
            sys.stdout.buffer.write(result.stdout)
            report_bytes = report.read_bytes() if report.exists() else b""
            sys.stdout.buffer.write(report_bytes)
            info = json.loads(ready.read_text())
            stats = {}
            for _ in range(60):
                with socket.create_connection(("127.0.0.1", info["control"]), timeout=3) as peer:
                    peer.sendall(b"stats\n")
                    data = bytearray()
                    while not data.endswith(b"\n"):
                        data.extend(peer.recv(65536))
                    stats = json.loads(data)
                if not stats["active_transports"] and not stats["active_relays"]:
                    break
                time.sleep(0.05)
            print("Fixture audit:", json.dumps(stats, sort_keys=True))
            assert all(stats[name] == 0 for name in ["exec", "sftp", "pty", "shell", "active_transports", "active_relays"]), stats
            assert stats["relay_peak"] <= 65536, stats
            assert all(window == 65536 for window in stats["windows"]), stats
            report_dir = os.environ.get("RABBITSHELL_TEST_REPORT_DIR")
            if report_dir:
                output = pathlib.Path(report_dir)
                output.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(report, output / "tunnel-tests.txt")
                (output / "tunnel-audit.json").write_text(json.dumps(stats, indent=2))
            totals = re.search(rb"Totals: (\d+) passed, (\d+) failed, (\d+) skipped", report_bytes)
            return result.returncode or (0 if totals and totals[2] == b"0" and totals[3] == b"0" else 1)
        finally:
            stop_fixture(fixture)


if __name__ == "__main__":
    sys.exit(main())
