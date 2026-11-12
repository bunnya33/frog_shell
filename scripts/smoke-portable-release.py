"""Small release smoke: real version/manager/demo only, no old wrapper matrix."""
import argparse,json,tempfile,time
from pathlib import Path
from portable_isolation import Sandbox,OwnedProcess
from portable_format import sha256
parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--manifest',type=Path,required=True);parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
metadata=json.loads(args.manifest.read_text(encoding='utf-8'));args.report.mkdir(parents=True,exist_ok=True)
results=[]
with tempfile.TemporaryDirectory(prefix='rs-release-') as tmp:
    box=Sandbox(Path(tmp),'unicode-release',args.exe.resolve(),metadata)
    with OwnedProcess(box,'version',['--version']) as process:
        r=process.wait();assert r['exit_code']==0 and metadata.get('display_name','RabbitShell')+' '+metadata['version'] in r['stdout'];results.append(r)
    box.validate_cache()
    for name,options in [('manager',[]),('demo',['--demo'])]:
        with OwnedProcess(box,name,box.args(*options)) as process:
            deadline=time.monotonic()+20
            while len(process.pids())<2 and time.monotonic()<deadline:process.sample();time.sleep(.025)
            assert len(process.pids())==2,'Qt child missing'
            time.sleep(.6);sample=process.sample();assert len(process.pids())==2
            process.stop_child(73);r=process.wait();assert r['exit_code']==73;r['alive_sample']=sample;r['scope']='Actual Qt startup, offscreen; only owned child terminated, not graceful UI exit';results.append(r)
        box.validate_cache()
    assert list(box.download.iterdir())==[box.exe]
(args.report/'portable-smoke.json').write_text(json.dumps(dict(status='PASS',version=metadata['version'],sha256=sha256(args.exe.read_bytes()),cases=results,minimal_path=True,real_servers=False,visible_desktop=False),ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(status='PASS',cases=len(results),version=metadata['version'])))
