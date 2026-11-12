"""Loopback-only SSH/SFTP test fixture. Never executes shell commands."""
import argparse
import json
import os
import pathlib
import socket
import threading
import time
import struct
import stat
import subprocess
import paramiko

parser = argparse.ArgumentParser()
parser.add_argument('--ready', required=True)
parser.add_argument('--audit-sftp', action='store_true')
parser.add_argument('--upload-fixture', action='store_true')
parser.add_argument('--bookmark-shell', help='Explicit private MSYS bash executable; no user shell startup files')
parser.add_argument('--clock-fixture', action='store_true', help='Return fixed clock samples; never execute clock commands')
args = parser.parse_args()
ready = pathlib.Path(args.ready).resolve()
root = ready.parent / 'remote'
root.mkdir(parents=True, exist_ok=True)
(root / 'config.json').write_bytes(b'{"version":1}\n')
(root / 'folder').mkdir(exist_ok=True)
(root / 'folder' / 'nested.txt').write_bytes(b'nested\n')
key = paramiko.RSAKey.generate(2048)
clock_audit_lock = threading.Lock()
clock_started = time.monotonic()


def clock_audit(server, event, **values):
    if args.clock_fixture:
        with clock_audit_lock:
            with (ready.parent / 'clock-audit.jsonl').open('a', encoding='utf-8') as audit:
                audit.write(json.dumps(dict(transport=id(server), event=event, **values)) + '\n')


def sftp_audit(operation,path):
    if args.audit_sftp:
        with (ready.parent/'sftp-audit.jsonl').open('a',encoding='utf-8') as audit:
            audit.write(json.dumps({'operation':operation,'path':path})+'\n')

class Files(paramiko.SFTPServerInterface):
    def local(self, path):
        result = (root / path.lstrip('/')).resolve()
        if result != root and root not in result.parents:
            raise PermissionError('Outside fixture root')
        return result

    def canonicalize(self, path):
        return '/' + self.local(path).relative_to(root).as_posix().replace('.', '', 1) if path == '.' else '/' + self.local(path).relative_to(root).as_posix()

    def list_folder(self, path):
        sftp_audit("list",path)
        try:
            result = []
            for entry in self.local(path).iterdir():
                attr = paramiko.SFTPAttributes.from_stat(entry.lstat())
                attr.filename = entry.name
                result.append(attr)
                if args.upload_fixture and path == '/root/case-collision' and entry.name == 'A.txt':
                    duplicate = paramiko.SFTPAttributes.from_stat(entry.lstat())
                    duplicate.filename = 'a.txt'
                    result.append(duplicate)
            if args.upload_fixture and path == '/root/link-skip':
                link = paramiko.SFTPAttributes()
                link.filename = 'fixture-link'
                link.st_mode = stat.S_IFLNK | 0o777
                result.append(link)
            return result
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    def stat(self, path):
        sftp_audit("stat",path)
        if args.upload_fixture and path.endswith('/stat-denied.txt'):
            return paramiko.SFTP_PERMISSION_DENIED
        try:
            return paramiko.SFTPAttributes.from_stat(self.local(path).stat())
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    lstat = stat

    def open(self, path, flags, attr):
        sftp_audit("open",path)
        try:
            fd = os.open(self.local(path), flags | getattr(os, 'O_BINARY', 0), 0o600)
            mode = 'r+b' if flags & os.O_RDWR else ('wb' if flags & os.O_WRONLY else 'rb')
            stream = os.fdopen(fd, mode)
            class MeterHandle(paramiko.SFTPHandle):
                def read(self, offset, length):
                    if 'meter-slow' in path: time.sleep(0.025)
                    return super().read(offset, length)
                def write(self, offset, data):
                    if 'meter-slow' in path: time.sleep(0.025)
                    return super().write(offset, data)
            handle = MeterHandle(flags)
            handle.readfile = stream
            handle.writefile = stream
            return handle
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    def mutate(self, func, *paths):
        try:
            func(*(self.local(p) for p in paths))
            return paramiko.SFTP_OK
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    def remove(self, path):
        return self.mutate(os.unlink, path)

    def mkdir(self, path, attr):
        return self.mutate(os.mkdir, path)

    def rmdir(self, path):
        return self.mutate(os.rmdir, path)

    def rename(self, old, new):
        sftp_audit("rename-no-overwrite",new)
        if args.upload_fixture and new in ('/root/race.txt','/root/race-cancel.txt') and not self.local(new).exists():
            self.local(new).write_bytes(b'RACE')
            return paramiko.SFTP_FAILURE
        if args.upload_fixture and new == '/root/rename-denied.txt':
            return paramiko.SFTP_PERMISSION_DENIED
        if args.upload_fixture and self.local(new).exists():
            return paramiko.SFTP_FAILURE
        return self.mutate(os.rename, old, new)

    def posix_rename(self, old, new):
        sftp_audit("posix-rename",new)
        return self.mutate(os.replace, old, new)

    def chattr(self, path, attr):
        try:
            paramiko.SFTPServer.set_file_attr(str(self.local(path)), attr)
            return paramiko.SFTP_OK
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)


class Server(paramiko.ServerInterface):
    def check_auth_password(self, user, password):
        return paramiko.AUTH_SUCCESSFUL if (user, password) == ('fixture', 'fixture-only') else paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return 'password,publickey'

    def check_auth_publickey(self, username, offered):
        return paramiko.AUTH_SUCCESSFUL if username == 'fixture' and offered == client_key else paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes):
        clock_audit(self, 'pty')
        return True

    def check_channel_window_change_request(self, *args):
        return True

    def check_channel_shell_request(self, channel):
        clock_audit(self, 'shell')
        if args.bookmark_shell:
            def shell():
                private_home=ready.parent/'shell-home';private_home.mkdir(exist_ok=True)
                env={'SYSTEMROOT':os.environ.get('SYSTEMROOT','C:/Windows'),'PATH':str(pathlib.Path(args.bookmark_shell).parent),
                     'HOME':str(private_home),'LANG':'C.UTF-8','RS_FIXTURE_ROOT':str(root).replace('\\','/')}
                process=subprocess.Popen([args.bookmark_shell,'--noprofile','--norc'],cwd=root,env=env,
                    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                # Map the isolated SFTP root into the real local shell; builtin cd
                # still parses and resolves the actual quoted command argument.
                process.stdin.write(b'cd() { if [[ "$1" == -- ]]; then shift; fi; builtin cd -- "$RS_FIXTURE_ROOT/${1#/}"; }\n');process.stdin.flush()
                def output():
                    try:
                        while True:
                            block=process.stdout.read1(4096)
                            if not block:break
                            channel.sendall(block)
                    except (EOFError,OSError):pass
                threading.Thread(target=output,daemon=True).start()
                try:
                    while True:
                        data=channel.recv(16384)
                        if not data:break
                        process.stdin.write(data.replace(b'\r',b'\n'));process.stdin.flush()
                except (EOFError,OSError):pass
                finally:
                    process.stdin.close()
                    try:process.wait(timeout=2)
                    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=2)
                    channel.close()
            threading.Thread(target=shell,daemon=True).start()
            return True
        def echo():
            try:
                channel.send(b'fixture$ ')
                while True:
                    data = channel.recv(16384)
                    if not data:
                        break
                    channel.sendall(data)
            except (EOFError, OSError):
                pass
        threading.Thread(target=echo, daemon=True).start()
        return True

    def check_channel_exec_request(self, channel, command):
        is_clock = args.clock_fixture and b"printf 'CLOCK|%s\\n'" in command
        is_write = args.clock_fixture and (b'timedatectl set-time' in command or b'date -s' in command)
        clock_audit(self, 'exec', clock=is_clock, write=is_write)
        with (ready.parent / 'exec-audit.jsonl').open('a', encoding='utf-8') as audit:
            audit.write(json.dumps({'command': command.decode('utf-8', errors='replace')}) + '\n')
        def output():
            try:
                time.sleep(0.02)
                if is_clock:
                    epoch = 1704067200 + int(time.monotonic() - clock_started)
                    date = time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(epoch + 8 * 3600))
                    channel.sendall(f'CLOCK|{epoch}|{date}|+0800|CST\nNTP|no\n'.encode())
                elif is_write:
                    channel.sendall_stderr(b'Clock mutations are forbidden in the read-only fixture\n')
                else:
                    channel.sendall(b'fixture-exec\n')
                channel.send_exit_status(1 if is_write else 0)
                channel.close()
            except (EOFError, OSError):
                pass
        threading.Thread(target=output, daemon=True).start()
        return True



class SftpProtocol(paramiko.SFTPServer):
    def _send_server_version(self):
        packet_type, data = self._read_packet()
        if packet_type != paramiko.sftp.CMD_INIT:
            raise paramiko.SFTPError('Expected SFTP initialization')
        version = struct.unpack('>I', data[:4])[0]
        message = paramiko.Message()
        message.add_int(3)
        # Files.posix_rename implements this extension with os.replace.
        message.add_string('posix-rename@openssh.com')
        message.add_string('1')
        self._send_packet(paramiko.sftp.CMD_VERSION, message)
        return version


def serve(client):
    transport = paramiko.Transport(client)
    transport.add_server_key(key)
    transport.set_subsystem_handler('sftp', SftpProtocol, Files)
    try:
        transport.start_server(server=Server())
        while transport.is_active():
            time.sleep(0.1)
    except (EOFError, OSError, paramiko.SSHException):
        pass
    finally:
        transport.close()


listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(8)
ready.parent.mkdir(parents=True, exist_ok=True)
client_key = paramiko.RSAKey.generate(2048)
client_key_path = ready.parent / 'synthetic-encrypted-key'
client_key.write_private_key_file(str(client_key_path), password='fixture-passphrase')
ready.write_text(json.dumps({'port': listener.getsockname()[1], 'root': str(root), 'key': str(client_key_path)}), encoding='utf-8')
print('Loopback fixture ready', flush=True)
while True:
    client, _ = listener.accept()
    threading.Thread(target=serve, args=(client,), daemon=True).start()
