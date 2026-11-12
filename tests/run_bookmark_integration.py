"""Private loopback with an explicitly isolated real bash shell, no startup files."""
import json,os,pathlib,subprocess,sys,tempfile,time
test=pathlib.Path(sys.argv[1]).resolve();report=pathlib.Path(os.environ['RABBITSHELL_TEST_REPORT_DIR']).resolve()
bash=pathlib.Path('C:/Program Files/Git/bin/bash.exe')
if not bash.is_file():raise RuntimeError('Explicit private fixture bash is required')
hidden={'creationflags':subprocess.CREATE_NO_WINDOW} if os.name=='nt' else {}
with tempfile.TemporaryDirectory(prefix='rs-bookmark-') as tmp:
    directory=pathlib.Path(tmp);ready=directory/'ready.json'
    server=subprocess.Popen([sys.executable,str(pathlib.Path(__file__).with_name('ssh_fixture.py')),'--ready',str(ready),'--audit-sftp','--bookmark-shell',str(bash)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,**hidden)
    try:
        deadline=time.monotonic()+15
        while not ready.exists():
            if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Fixture failed to start')
            time.sleep(.025)
        fixture=json.loads(ready.read_text(encoding='utf-8'));remote=pathlib.Path(fixture['root'])
        for path in ["bookmarks/中文 space/it's $(safe)",'bookmarks/empty']:(remote/path).mkdir(parents=True,exist_ok=True)
        env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',RABBITSHELL_BOOKMARK_PORT=str(fixture['port']))
        result=subprocess.run([str(test),'isolatedBookmarkPty','-o',str(report/'bookmark-sftp.txt')+',txt'],env=env,capture_output=True,timeout=35,**hidden)
        audit=directory/'sftp-audit.jsonl';(report/'bookmark-sftp-audit.jsonl').write_bytes(audit.read_bytes())
        assert not (directory/'exec-audit.jsonl').exists(),'Unexpected exec'
        (report/'bookmark-sftp-summary.json').write_text(json.dumps({'exit_code':result.returncode,'remote_exec':0,'actual_shell':'MSYS bash --noprofile --norc with private HOME and mapped fixture root','scope':'Private loopback SSH/SFTP only, no real user connection or trust/settings'},indent=2),encoding='utf-8')
        print((report/'bookmark-sftp.txt').read_text(encoding='utf-8'))
        if result.returncode:raise SystemExit(result.returncode)
    finally:
        server.terminate();server.communicate(timeout=5)
