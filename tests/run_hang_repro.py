"""Bounded watchdog for our own offscreen synthetic Qt child, never a user process."""
import argparse,ctypes,json,os,pathlib,subprocess,time
from ctypes import wintypes

parser=argparse.ArgumentParser();parser.add_argument('binary',type=pathlib.Path);parser.add_argument('case');parser.add_argument('report',type=pathlib.Path)
parser.add_argument('--hidden-windows',action='store_true')
args=parser.parse_args();args.report.mkdir(parents=True,exist_ok=True)
class Memory(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD),('peak_ws',ctypes.c_size_t),('ws',ctypes.c_size_t),('peak_paged',ctypes.c_size_t),('paged',ctypes.c_size_t),('peak_nonpaged',ctypes.c_size_t),('nonpaged',ctypes.c_size_t),('pagefile',ctypes.c_size_t),('peak_pagefile',ctypes.c_size_t),('private',ctypes.c_size_t)]
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.K32GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD]
kernel.GetProcessTimes.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.FILETIME),ctypes.POINTER(wintypes.FILETIME),ctypes.POINTER(wintypes.FILETIME),ctypes.POINTER(wintypes.FILETIME)]
env=os.environ.copy();env.update(QT_QPA_PLATFORM='offscreen',RABBITSHELL_TEST_REPORT_DIR=str(args.report.resolve()))
if args.hidden_windows:env.update(QT_QPA_PLATFORM='windows',RABBITSHELL_HIDDEN_WINDOWS='1')
samples=[];reason='completed';start=time.monotonic()
with (args.report/'watchdog-child-output.txt').open('wb') as output:
    child=subprocess.Popen([str(args.binary.resolve()),args.case,'-o',str(args.report/'qt-report.txt')+',txt'],env=env,stdout=output,stderr=output,creationflags=subprocess.CREATE_NO_WINDOW)
    while child.poll() is None:
        memory=Memory();memory.cb=ctypes.sizeof(memory);assert kernel.K32GetProcessMemoryInfo(child._handle,ctypes.byref(memory),ctypes.sizeof(memory))
        times=[wintypes.FILETIME() for _ in range(4)];assert kernel.GetProcessTimes(child._handle,*map(ctypes.byref,times))
        cpu=sum((v.dwHighDateTime<<32)+v.dwLowDateTime for v in times[2:])/10_000_000
        elapsed=time.monotonic()-start;samples.append(dict(ms=round(elapsed*1000),private_bytes=memory.private,working_set=memory.ws,cpu_seconds=cpu))
        if memory.private>512*1024*1024 or elapsed>12:
            reason='private-memory-cap' if memory.private>512*1024*1024 else 'timeout';child.kill();break
        time.sleep(.05)
    child.wait(timeout=5)
result=dict(scope='Only watchdog-owned synthetic child',platform='hidden-windows' if args.hidden_windows else 'offscreen',case=args.case,exit_code=child.returncode,stop_reason=reason,peak_private=max((s['private_bytes'] for s in samples),default=0),samples=samples)
(args.report/'watchdog.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='samples'}))
raise SystemExit(0 if child.returncode==0 and reason=='completed' else 1)
