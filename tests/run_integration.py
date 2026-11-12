"""Run protocol tests against a fresh, isolated loopback fixture."""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

binary = pathlib.Path(sys.argv[1]).resolve()
hidden_process = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
with tempfile.TemporaryDirectory(prefix='rabbitshell-test-') as directory:
    ready = pathlib.Path(directory) / 'ready.json'
    server = subprocess.Popen([sys.executable, str(pathlib.Path(__file__).with_name('ssh_fixture.py')),
                               '--ready', str(ready)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, **hidden_process)
    try:
        deadline = time.monotonic() + 15
        while not ready.exists():
            if server.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('SSH test fixture did not start')
            time.sleep(0.05)
        env = os.environ.copy()
        fixture = json.loads(ready.read_text(encoding='utf-8'))
        env['RABBITSHELL_TEST_PORT'] = str(fixture['port'])
        env['RABBITSHELL_TEST_KEY'] = fixture['key']
        env['QT_QPA_PLATFORM'] = 'offscreen'
        report = pathlib.Path(directory) / 'session-test.txt'
        try:
            result = subprocess.run([str(binary), *sys.argv[2:], '-o', str(report) + ',txt'], env=env, timeout=120,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **hidden_process)
        except subprocess.TimeoutExpired as error:
            sys.stdout.buffer.write(error.output or b'')
            if report.exists():
                sys.stdout.buffer.write(report.read_bytes())
            raise
        sys.stdout.buffer.write(result.stdout)
        report_bytes = report.read_bytes() if report.exists() else b''
        sys.stdout.buffer.write(report_bytes)
        report_directory = os.environ.get('RABBITSHELL_TEST_REPORT_DIR')
        if report_directory and report.exists():
            target = pathlib.Path(report_directory)
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(report, target / (binary.stem + '.txt'))
        audit_path = pathlib.Path(directory) / 'exec-audit.jsonl'
        audit_text = audit_path.read_text(encoding='utf-8') if audit_path.exists() else ''
        if 'RS_PROC_1' in audit_text:
            raise AssertionError('Terminal startup/close unexpectedly executed the removed process probe')
        if report_directory:
            (target / (binary.stem + '-exec-audit.json')).write_text(json.dumps({
                'removed_close_probe_requests': 0,
                'exec_requests': len(audit_text.splitlines()),
                'scope': 'Fresh loopback fixture; SSH exec commands are never executed'
            }, indent=2), encoding='utf-8')
        totals = re.search(rb'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', report_bytes)
        skips = [line for line in report_bytes.splitlines() if line.startswith(b'SKIP')]
        expected_skips = all(b'nativeStoreAcrossProcess' in line for line in skips)
        exit_code = result.returncode or (0 if totals and totals[2] == b'0' and expected_skips else 1)
    finally:
        server.terminate()
        server.communicate(timeout=5)
sys.exit(exit_code)
