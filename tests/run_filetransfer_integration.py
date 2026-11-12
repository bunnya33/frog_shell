"""Private loopback SFTP only; does not read user trust/credentials or drive a desktop."""
import json,os,pathlib,subprocess,sys,tempfile,time

root=pathlib.Path(__file__).resolve().parent
binary=pathlib.Path(sys.argv[1]).resolve()
report=pathlib.Path(os.environ['RABBITSHELL_TEST_REPORT_DIR']).resolve()
hidden={'creationflags':subprocess.CREATE_NO_WINDOW} if os.name=='nt' else {}
with tempfile.TemporaryDirectory(prefix='rs-upload-') as temporary:
    directory=pathlib.Path(temporary);ready=directory/'ready.json'
    server=subprocess.Popen([sys.executable,str(root/'ssh_fixture.py'),'--ready',str(ready),'--audit-sftp','--upload-fixture'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,**hidden)
    try:
        deadline=time.monotonic()+15
        while not ready.exists():
            if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Fixture did not start')
            time.sleep(.025)
        fixture=json.loads(ready.read_text(encoding='utf-8'));remote=pathlib.Path(fixture['root'])
        for path in ['root/A/deep','root/B','root/type-dir','root/recursive']:(remote/path).mkdir(parents=True,exist_ok=True)
        for path in ['root/root.txt','root/A/deep/external.txt','root/collision.txt','root/skip.txt','root/batch1.txt','root/batch2.txt','root/type-file','root/recursive/existing.txt']:(remote/path).write_bytes(b'OLD')
        for i in range(80):(remote/f'root/bin/sub-{i}').mkdir(parents=True,exist_ok=True)
        (remote/'root/bin/sub-0/keep').write_bytes(b'keep')
        env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',RABBITSHELL_UPLOAD_PORT=str(fixture['port']),RABBITSHELL_UPLOAD_ROOT=str(remote))
        result=subprocess.run([str(binary),*os.environ.get('RABBITSHELL_UPLOAD_CASES','isolatedUploadProtocol isolatedEventsRefreshAndWaitingClose isolatedUploadThenDelete').split(),'-o',str(report/'sftp-integration.txt')+',txt'],env=env,capture_output=True,timeout=25,**hidden)
        audit=(directory/'sftp-audit.jsonl').read_text(encoding='utf-8');(report/'sftp-audit.jsonl').write_text(audit,encoding='utf-8')
        assert not (directory/'exec-audit.jsonl').exists(),'Unexpected exec'
        assert not list(remote.rglob('*.part')),'Temporary file left'
        assert not list(remote.rglob('.rabbitshell-recovery-*')),'Unexpected remote recovery file'
        (report/'sftp-integration-summary.json').write_text(json.dumps({'exit_code':result.returncode,'remote_exec':0,'temporary_uploads_remaining':0,'scope':'Private loopback SSH/SFTP, hidden/offscreen Qt. No real server or user credential/trust files.'},indent=2),encoding='utf-8')
        print((report/'sftp-integration.txt').read_text(encoding='utf-8'))
        if result.returncode:raise SystemExit(result.returncode)
    finally:
        server.terminate();server.communicate(timeout=5)
