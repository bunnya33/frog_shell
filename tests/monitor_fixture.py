"""Loopback protocol delays only; no shell, user data, or external connections."""
import argparse,json,socket,threading,time
from pathlib import Path
import paramiko
args=argparse.ArgumentParser();args.add_argument('--ready',required=True);args.add_argument('--scenario',required=True);args=args.parse_args()
ready=Path(args.ready);audit=ready.with_name('monitor-audit.jsonl');guard=threading.Lock();key=paramiko.RSAKey.generate(2048)
def record(event,**fields):
    with guard:
        with audit.open('a',encoding='utf-8') as f:f.write(json.dumps(dict(event=event,**fields))+'\n')
class Files(paramiko.SFTPServerInterface):
    def canonicalize(self,path):return '/' if path=='.' else path
    def list_folder(self,path):return []
class Transport(paramiko.Transport):
    def __init__(self,sock):super().__init__(sock);self.opens=0;self.exec_channels={}
    def _send_message(self,data):
        raw=data.asbytes()
        if raw[:1]==bytes([91]):
            self.opens+=1
            if self.opens==3 and args.scenario=='channel-open':
                def release():
                    record('open-wait',active=self.is_active());time.sleep(7)
                    if self.is_active():super(Transport,self)._send_message(data);record('open-resumed')
                threading.Thread(target=release,daemon=True).start();return
        return super()._send_message(data)
    def _send_user_message(self,data):
        raw=data.asbytes();cid=int.from_bytes(raw[1:5],'big') if len(raw)>=5 else -1
        if raw[:1]==bytes([99]) and cid in self.exec_channels:
            channel=self.exec_channels.pop(cid)
            def release():
                if args.scenario=='peer-close':time.sleep(.5);self.close();return
                if args.scenario=='user-stop':return
                if args.scenario=='exec-recover':time.sleep(7)
                if self.is_active():
                    super(Transport,self)._send_user_message(data)
                    if args.scenario=='read-close':time.sleep(9)
                    if not self.is_active():return
                    channel.sendall(b'PRIVATE_MONITOR_RESULT\n');channel.send_exit_status(0);channel.shutdown_write();channel.close()
            threading.Thread(target=release,daemon=True).start();return
        return super()._send_user_message(data)
class Server(paramiko.ServerInterface):
    def check_auth_password(self,u,p):return paramiko.AUTH_SUCCESSFUL if (u,p)==('fixture','fixture-only') else paramiko.AUTH_FAILED
    def get_allowed_auths(self,u):return 'password'
    def check_channel_request(self,k,n):return paramiko.OPEN_SUCCEEDED
    def check_channel_pty_request(self,*a):return True
    def check_channel_window_change_request(self,*a):return True
    def check_channel_shell_request(self,ch):
        def echo():
            try:
                ch.sendall(b'private-fixture$ ')
                while data:=ch.recv(65536):record('input',bytes=len(data));ch.sendall(data)
            except (OSError,EOFError):pass
        threading.Thread(target=echo,daemon=True).start();return True
    def check_channel_exec_request(self,ch,cmd):ch.get_transport().exec_channels[ch.remote_chanid]=ch;return True
def serve(client):
    t=Transport(client);t.add_server_key(key);t.set_subsystem_handler('sftp',paramiko.SFTPServer,Files)
    try:
        t.start_server(server=Server())
        while t.is_active():time.sleep(.05)
    except (OSError,EOFError,paramiko.SSHException):pass
    finally:t.close()
s=socket.socket();s.bind(('127.0.0.1',0));s.listen(8);ready.write_text(json.dumps({'port':s.getsockname()[1]}))
while True:
    c,_=s.accept();threading.Thread(target=serve,args=(c,),daemon=True).start()
