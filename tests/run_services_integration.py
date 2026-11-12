import json,os,pathlib,subprocess,sys,tempfile,time,shutil
root=pathlib.Path(__file__).resolve().parent
hidden={'creationflags':subprocess.CREATE_NO_WINDOW} if os.name=='nt' else {}
report=pathlib.Path(os.environ.get('RABBITSHELL_TEST_REPORT_DIR','test-results/0.1.14')).resolve();report.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='rs-service-fixture-') as tmp:
    ready=pathlib.Path(tmp)/'ready.json'
    fixture=subprocess.Popen([sys.executable,str(root/'service_fixture.py'),'--ready',str(ready)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,**hidden)
    try:
        deadline=time.monotonic()+15
        while not ready.exists():
            if fixture.poll() is not None or time.monotonic()>deadline:raise RuntimeError('fixture startup: '+fixture.stderr.read().decode(errors='replace'))
            time.sleep(.03)
        env=os.environ.copy();env['QT_QPA_PLATFORM']='offscreen';env['RABBITSHELL_SERVICE_PORT']=str(json.loads(ready.read_text())['port'])
        result=subprocess.run([sys.argv[1],*sys.argv[2:],'-o',str(report/'services-integration.txt')+',txt'],env=env,capture_output=True,timeout=120,**hidden)
        sys.stdout.buffer.write((report/'services-integration.txt').read_bytes())
        audit=pathlib.Path(tmp)/'service-audit.jsonl';rows=[json.loads(v) for v in audit.read_text(encoding='utf-8').splitlines()] if audit.exists() else []
        if audit.exists():shutil.copyfile(audit,report/'services-command-stdin-audit.jsonl')
        if result.returncode:sys.exit(result.returncode)
        assert all('sudo ' not in r['command'] and '--now' not in r['command'] and 'rm -r' not in r['command'] for r in rows)
        creates=[r for r in rows if r['action']=='create'];assert len(creates)==1,creates
        assert '[Service]\nType=simple\nUser=fixture\n' in creates[0]['stdin']
        assert '$$HOME' in creates[0]['stdin'] and '%%n' in creates[0]['stdin']
        assert all(r['stdin']=='' for r in rows if r['action']!='create')
        assert sum(r['action']=='delete' for r in rows)==1
        assert not any("'apport-coredump-hook@.service'" in r['command'] for r in rows), 'Template sent to remote command'
        assert any("'apport-coredump-hook@worker.service'" in r['command'] for r in rows)
        (report/'services-fixture-summary.json').write_text(json.dumps(dict(exit_code=result.returncode,requests=len(rows),creates=len(creates),deletes=1,shell_executed=False,real_systemd=False),indent=2),encoding='utf-8')
        sys.exit(result.returncode)
    finally:fixture.terminate();fixture.communicate(timeout=5)
