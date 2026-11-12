"""Loopback SSH/SFTP protocol fixture. Interprets a fixed command vocabulary; NEVER executes shell/systemctl."""
import argparse, json, pathlib, socket, threading, time, shlex, stat
import paramiko

parser=argparse.ArgumentParser();parser.add_argument('--ready',required=True);args=parser.parse_args()
ready=pathlib.Path(args.ready);audit=ready.parent/'service-audit.jsonl';lock=threading.Lock()
key=paramiko.RSAKey.generate(2048)
def unit(name, active='inactive', sub='dead', boot='disabled', path=None):
    return dict(Id=name,Names=name,LoadState='loaded',ActiveState=active,SubState=sub,UnitFileState=boot,
                Description='fixture '+name,FragmentPath=path or '/etc/systemd/system/'+name,SourcePath='',
                DropInPaths='',Transient='no',TriggeredBy='',CanStart='yes',CanStop='yes',CanReload='yes')
units={'demo.service':unit('demo.service','active','running','enabled'),
       'installed.service':unit('installed.service'), 'oneshot.service':unit('oneshot.service','active','exited','static'),
       'denied.service':unit('denied.service'),
       'apport-coredump-hook@worker.service':unit('apport-coredump-hook@worker.service','active','running','enabled')}
template='apport-coredump-hook@.service'
fingerprint='1:2:81a4:0:0:1:400:100:100\n'+'a'*64+'  -\n'
def properties(d):return ''.join(k+'='+v+'\n' for k,v in d.items())
def interpret(text,data,user='fixture'):
    tokens=shlex.split(text);code=0;out='';err='';action='query'
    if 'systemctl' in tokens:
        t=tokens[tokens.index('systemctl')+1:]
        action=next((v for v in t if v in {'show','list-units','list-unit-files','daemon-reload','start','stop','restart','reload','enable','disable'}),'capability')
        names=t[t.index('--')+1:] if '--' in t else []
        if action=='show':
            if user=='no-manager':code=1;err='System has not been booted with systemd as init system. Failed to connect to bus.\n'
            elif not names:out='Version=255\n'
            elif user=='query-denied':code=1;err='Failed to get properties: Access denied\n'
            else:
                blocks=[]
                for n in names:
                    if n==template:
                        code=1;err+='Failed to get properties: Unit name '+n+' is neither a valid invocation ID nor unit name.\n'
                        break
                    elif n=='vanished.service':
                        code=1;err+='Failed to get properties: Unit '+n+' not found.\n'
                        break
                    else:blocks.append(properties(units.get(n,dict(Id=n,Names=n,LoadState='not-found',FragmentPath='',ActiveState='inactive',SubState='dead',UnitFileState=''))))
                out='\n'.join(blocks)
        elif action=='list-unit-files':
            installed={**{n:d['UnitFileState'] for n,d in units.items()},template:'static','vanished.service':'disabled'}
            out=''.join(n+' '+boot+' -\n' for n,boot in installed.items() if not names or n in names)
        elif action=='list-units':out=''.join(n+' loaded '+d['ActiveState']+' '+d['SubState']+' fixture\n' for n,d in units.items() if n!='installed.service')
        elif action!='daemon-reload' and names:
            n=names[0]
            if n=='denied.service':code=1;err='Access denied: interactive authorization disabled\n'
            else:
                d=units[n]
                if action in ('start','restart'):d.update(ActiveState='active',SubState='running')
                elif action=='stop':d.update(ActiveState='inactive',SubState='dead')
                elif action=='enable':d['UnitFileState']='enabled'
                elif action=='disable':d['UnitFileState']='disabled'
    elif 'journalctl' in tokens:
        action='logs';name=next(v.split('=',1)[1] for v in tokens if v.startswith('--unit='))
        if name=='drop.service':return ('disconnect',b'',b'',-1)
        if name=='slow.service':time.sleep(16)
        out='x'*300000 if name=='huge.service' else 'fixture log\n'
    elif 'getent' in tokens:out='fixture:x:1000:1000:Fixture:/home/fixture:/bin/sh\n'
    elif 'id' in tokens:out='1000\n'
    elif 'set -C' in text or ('set' in tokens and '-C;' in tokens):
        action='create';name=next(t for t in tokens if t.startswith('p=')).split('=',1)[1].rstrip(';').split('/')[-1]
        if name in units:code=1;err='File exists'
        else:units[name]=unit(name)
    elif 'rm --' in text:
        action='delete';name=next(t for t in tokens if t.startswith('p=')).split('=',1)[1].rstrip(';').split('/')[-1];units.pop(name,None)
    elif 'sha256sum' in text:out=fingerprint
    elif 'stat' in tokens:
        name=tokens[-1];out='81ed:1000:1000\n' if name.endswith('/run') or name.startswith('/usr/bin/') else '41ed:1000:1000\n'
    else:code=127;err='Fixture rejected unknown command'
    return action,out.encode(),err.encode(),code

class Files(paramiko.SFTPServerInterface):
    def canonicalize(self,path):return '/' if path in ('.','/') else path
    def list_folder(self,path):
        a=paramiko.SFTPAttributes();a.filename='folder' if path=='/' else 'program';a.st_mode=(stat.S_IFDIR|0o755) if path=='/' else (stat.S_IFREG|0o755);a.st_size=100;a.st_uid=a.st_gid=1000;a.st_mtime=1;a.st_atime=1;return [a]
class Server(paramiko.ServerInterface):
    def check_auth_password(self,user,password):self.user=user;return paramiko.AUTH_SUCCESSFUL if user in ('fixture','files','query-denied','no-manager') and password=='fixture-only' else paramiko.AUTH_FAILED
    def get_allowed_auths(self,user):return 'password'
    def check_channel_request(self,kind,chanid):return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_subsystem_request(self,ch,name):return super().check_channel_subsystem_request(ch,name) if self.user=='files' else False
    def check_channel_exec_request(self,ch,command):
        def respond():
            try:
                data=b''
                while chunk:=ch.recv(8192):
                    data+=chunk
                    if len(data)>32768:raise ValueError('stdin bound')
                text=command.decode('utf-8');action,out,err,code=interpret(text,data,self.user)
                with lock:
                    with audit.open('a',encoding='utf-8') as f:f.write(json.dumps(dict(command=text,stdin=data.decode('utf-8'),action=action,exit=code,stderr=err.decode('utf-8'),user=self.user),ensure_ascii=False)+'\n')
                if action=='disconnect':ch.get_transport().close();return
                if out:ch.sendall(out)
                if err:ch.sendall_stderr(err)
                ch.send_exit_status(code);ch.close()
            except (EOFError,OSError,paramiko.SSHException):pass
        threading.Thread(target=respond,daemon=True).start();return True
def serve(client):
    t=paramiko.Transport(client);t.add_server_key(key);t.set_subsystem_handler('sftp',paramiko.SFTPServer,Files)
    try:
        t.start_server(server=Server())
        while t.is_active():time.sleep(.05)
    except (EOFError,OSError,paramiko.SSHException):pass
    finally:t.close()
s=socket.socket();s.bind(('127.0.0.1',0));s.listen(8);ready.write_text(json.dumps(dict(port=s.getsockname()[1])),encoding='utf-8')
while True:
    client,_=s.accept();threading.Thread(target=serve,args=(client,),daemon=True).start()
