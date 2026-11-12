"""Read-only clock/interactive PTY coexistence through the real Workspace entry."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

binary = Path(sys.argv[1]).resolve()
output = Path(os.environ['RABBITSHELL_TEST_REPORT_DIR']).resolve()
output.mkdir(parents=True, exist_ok=True)
hidden = {'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}
with tempfile.TemporaryDirectory(prefix='frogshell-clock-') as temporary:
    folder = Path(temporary)
    ready = folder / 'ready.json'
    fixture = subprocess.Popen([sys.executable, str(Path(__file__).with_name('ssh_fixture.py')),
                                '--ready', str(ready), '--clock-fixture'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, **hidden)
    try:
        deadline = time.monotonic() + 20
        while not ready.exists():
            if fixture.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('Clock fixture did not start')
            time.sleep(.025)
        info = json.loads(ready.read_text(encoding='utf-8'))
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', RABBITSHELL_CLOCK_PORT=str(info['port']),
                   RABBITSHELL_DATA_DIR=str(folder / 'config'), SSH_AUTH_SOCK=str(folder / 'no-agent'))
        report = output / 'workspace-clock-tests.txt'
        run = subprocess.run([str(binary), 'loopbackClockKeepsInteractiveSession', '-o', str(report) + ',txt'],
                             env=env, capture_output=True, timeout=90, **hidden)
        (output / 'workspace-clock-console.txt').write_bytes(run.stdout + run.stderr)
        audit_path = folder / 'clock-audit.jsonl'
        audit = [json.loads(line) for line in audit_path.read_text().splitlines()] if audit_path.exists() else []
        (output / 'workspace-clock-audit.json').write_text(json.dumps(audit, indent=2), encoding='utf-8')
        clock = {r['transport'] for r in audit if r['event'] == 'exec' and r['clock']}
        pty = {r['transport'] for r in audit if r['event'] in ('pty', 'shell')}
        assert clock and pty and clock.isdisjoint(pty), audit
        assert sum(r['event'] == 'exec' and r['clock'] for r in audit) == 2, audit
        assert not any(r.get('write') for r in audit), audit
        content = report.read_bytes() if report.exists() else b''
        sys.stdout.buffer.write(content)
        totals = re.search(rb'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', content)
        raise SystemExit(run.returncode or (0 if totals and totals[2] == b'0' and totals[3] == b'0' else 1))
    finally:
        fixture.terminate()
        _, errors = fixture.communicate(timeout=5)
        (output / 'workspace-clock-fixture-stderr.txt').write_bytes(errors)
