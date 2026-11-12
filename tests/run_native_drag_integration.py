"""Only private loopback SFTP and COM consumption; never drives Explorer or the desktop."""
import hashlib,json,os,pathlib,subprocess,sys,tempfile,time
root=pathlib.Path(__file__).resolve().parent
binary=pathlib.Path(sys.argv[1]).resolve()
report=pathlib.Path(os.environ['RABBITSHELL_TEST_REPORT_DIR']).resolve()
hidden={'creationflags':subprocess.CREATE_NO_WINDOW} if os.name=='nt' else {}
with tempfile.TemporaryDirectory(prefix='rs-native-drag-') as temporary:
    directory=pathlib.Path(temporary);ready=directory/'ready.json'
    server=subprocess.Popen([sys.executable,str(root/'ssh_fixture.py'),'--ready',str(ready),'--audit-sftp'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,**hidden)
    try:
        deadline=time.monotonic()+15
        while not ready.exists():
            if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Fixture did not start')
            time.sleep(.025)
        fixture=json.loads(ready.read_text(encoding='utf-8'));remote=pathlib.Path(fixture['root'])
        with (remote/'drag-large.bin').open('wb') as stream:
            block=bytes(range(256))*256
            for _ in range(144):stream.write(block)
        (remote/'meter-slow-drag.bin').write_bytes(b'x'*2*1024*1024)
        env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',RABBITSHELL_DRAG_PORT=str(fixture['port']),RABBITSHELL_DRAG_ROOT=str(remote))
        result=subprocess.run([str(binary),'isolatedSftpDropAndChangedSize','isolatedSftpActiveWriterWorkspaceClose','-o',str(report/'native-sftp.txt')+',txt'],env=env,capture_output=True,timeout=90,**hidden)
        audit=(directory/'sftp-audit.jsonl').read_text(encoding='utf-8')
        (report/'native-sftp-audit.jsonl').write_text(audit,encoding='utf-8')
        entries=[json.loads(line) for line in audit.splitlines()]
        opens=[e for e in entries if e['operation']=='open' and e['path']=='/drag-large.bin']
        assert len(opens)==1,opens
        assert not (directory/'exec-audit.jsonl').exists(),'Unexpected remote command'
        (report/'native-sftp-audit.json').write_text(json.dumps({'regular_file_opens':len(opens),'remote_exec':0,'large_file_bytes':(remote/'drag-large.bin').stat().st_size,'large_file_sha256':hashlib.sha256((remote/'drag-large.bin').read_bytes()).hexdigest(),'scope':'Private loopback fixture. No real Explorer or user credentials.'},indent=2),encoding='utf-8')
        print((report/'native-sftp.txt').read_text(encoding='utf-8'))
        if result.returncode:raise SystemExit(result.returncode)
    finally:
        server.terminate();server.communicate(timeout=5)
