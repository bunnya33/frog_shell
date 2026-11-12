"""Private loopback tests: never executes a real shell or touches a real server."""
import json,os,pathlib,subprocess,sys,tempfile,time
root=pathlib.Path(__file__).resolve().parent
report=pathlib.Path(os.environ['RABBITSHELL_TEST_REPORT_DIR']).resolve()
hidden={'creationflags':subprocess.CREATE_NO_WINDOW} if os.name=='nt' else {}
with tempfile.TemporaryDirectory(prefix='rs-lock-integration-') as temporary:
    directory=pathlib.Path(temporary);processes=[];ports={}
    try:
        for name,script,options in [('service','service_fixture.py',[]),('upload','ssh_fixture.py',['--audit-sftp'])]:
            target=directory/name;target.mkdir();ready=target/'ready.json'
            server=subprocess.Popen([sys.executable,str(root/script),'--ready',str(ready),*options],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,**hidden);processes.append(server)
            deadline=time.monotonic()+15
            while not ready.exists():
                if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Private fixture failed to start')
                time.sleep(.025)
            ports[name]=json.loads(ready.read_text(encoding='utf-8'))
        remote=pathlib.Path(ports['upload']['root']);(remote/'root').mkdir()
        env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',RABBITSHELL_SERVICE_PORT=str(ports['service']['port']),RABBITSHELL_UPLOAD_PORT=str(ports['upload']['port']),RABBITSHELL_UPLOAD_ROOT=str(remote))
        result=subprocess.run([sys.argv[1],'existingServiceAndTransferLoopback','-o',str(report/'lock-loopback.txt')+',txt'],env=env,capture_output=True,timeout=60,**hidden)
        print((report/'lock-loopback.txt').read_text(encoding='utf-8'))
        rows=[json.loads(line) for line in (directory/'service/service-audit.jsonl').read_text(encoding='utf-8').splitlines()]
        assert any(row['action']=='restart' for row in rows)
        (report/'lock-loopback-summary.json').write_text(json.dumps(dict(exit_code=result.returncode,service_actions=[r['action'] for r in rows],bytes=(remote/'root/meter-slow-lock.bin').stat().st_size,scope='Two private loopback Paramiko servers; no real shell, systemd or user configuration.'),indent=2)+'\n',encoding='utf-8')
        if result.returncode:raise SystemExit(result.returncode)
    finally:
        for server in processes:server.terminate();server.communicate(timeout=5)
