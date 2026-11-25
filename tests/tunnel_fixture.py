"""Isolated loopback SSH direct-tcpip fixture. Never executes client commands.

The TCP relay has two 64 KiB queues and preserves both independent EOFs.
This is a Paramiko protocol fixture, not an OpenSSH policy test.
"""
import argparse
import base64
import hashlib
import json
import logging
import pathlib
import select
import socket
import sys
import threading
import time
import traceback

import paramiko

logging.getLogger("paramiko").setLevel(logging.CRITICAL)
CAP = 65536
CHUNK = 16384
PATTERN = bytes(range(256)) * 1024


class FixtureTransport(paramiko.Transport):
    def __init__(self, peer, fixture):
        self.fixture = fixture
        super().__init__(peer, default_window_size=CAP, default_max_packet_size=CHUNK)

    def _parse_global_request(self, message):
        if self.fixture.silence_keepalive and paramiko.Message(message.asbytes()).get_text() == "keepalive@openssh.com":
            self.fixture.count("keepalives")
            return
        super()._parse_global_request(message)


class Fixture:
    def __init__(self, directory, ui_sftp_rejection=False, hostkey_type="rsa"):
        self.directory = directory
        self.ui_sftp_rejection = ui_sftp_rejection
        self.lock = threading.Lock()
        self.keys = ([paramiko.ECDSAKey.generate(bits=256) for _ in range(2)] if hostkey_type == "ecdsa"
                     else [paramiko.RSAKey.generate(2048) for _ in range(2)])
        self.key_index = 0
        self.userkey = paramiko.RSAKey.generate(2048)
        self.userkey.write_private_key_file(str(directory / "identity"))
        self.userkey.write_private_key_file(str(directory / "encrypted"), password="fixture-key")
        self.transports = []
        self.outage = False
        self.silence_keepalive = False
        self.counters = dict(connections=0, authenticated=0, password_rejected=0, key_authenticated=0,
                             direct=0, denied=0, exec=0, sftp=0, pty=0, shell=0,
                             active_transports=0, active_relays=0, reverse_bytes=0,
                             relay_peak=0, halfclose_targets=0, windows=[], relays=[],
                             keepalives=0, refused_during_outage=0)
        self.targets = {}
        for mode in ["echo", "tail", "reverse", "sink", "source", "delay", "deny", "cut"]:
            listener = self.listener()
            self.targets[mode] = listener.getsockname()[1]
            threading.Thread(target=self.target_accept, args=(listener, mode), daemon=True).start()
        self.ssh_listener = self.listener()
        self.control = self.listener()
        threading.Thread(target=self.ssh_accept, daemon=True).start()
        threading.Thread(target=self.control_accept, daemon=True).start()

    @staticmethod
    def listener():
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(64)
        return listener

    def count(self, name, delta=1):
        with self.lock:
            self.counters[name] += delta

    def target_accept(self, listener, mode):
        while True:
            peer, _ = listener.accept()
            threading.Thread(target=self.target, args=(peer, mode), daemon=True).start()

    def target(self, peer, mode):
        try:
            peer.settimeout(30)
            if mode == "source":
                for _ in range(256):
                    peer.sendall(PATTERN)
                peer.shutdown(socket.SHUT_WR)
            if mode == "reverse":
                peer.sendall(b"REVERSE-EOF")
                peer.shutdown(socket.SHUT_WR)
            digest = hashlib.sha256()
            size = 0
            while True:
                data = peer.recv(CHUNK)
                if not data:
                    break
                digest.update(data)
                size += len(data)
                if mode == "sink":
                    time.sleep(0.02)
                if mode in ["echo", "delay"]:
                    peer.sendall(data)
                if mode == "cut":
                    self.cut()
                    return
            if mode == "tail":
                self.count("halfclose_targets")
                peer.sendall(b"TAIL:" + digest.hexdigest().encode() + b":" + PATTERN)
                peer.shutdown(socket.SHUT_WR)
            if mode == "reverse":
                self.count("reverse_bytes", size)
        except (OSError, EOFError):
            pass
        finally:
            peer.close()

    def cut(self):
        with self.lock:
            transports = list(self.transports)
        for transport in transports:
            transport.close()

    def check_idle_keepalive(self):
        with self.lock:
            transport = next((item for item in self.transports if item.is_authenticated()), None)
        if transport is None:
            return False
        reply = threading.Event()

        def request():
            try:
                transport.global_request("keepalive@openssh.com", wait=True)
                if transport.is_active():
                    reply.set()
            except (OSError, EOFError):
                pass

        threading.Thread(target=request, daemon=True).start()
        return reply.wait(1)

    def control_accept(self):
        while True:
            peer, _ = self.control.accept()
            with peer:
                try:
                    peer.settimeout(3)
                    command = peer.recv(100).decode().strip()
                    if command == "rotate":
                        with self.lock:
                            self.key_index = 1 - self.key_index
                    elif command == "cut":
                        self.cut()
                    elif command in ("outage", "resume"):
                        with self.lock:
                            self.outage = command == "outage"
                        if self.outage:
                            self.cut()
                    elif command in ("silence_keepalive", "reply_keepalive"):
                        self.silence_keepalive = command == "silence_keepalive"
                    idle_reply = self.check_idle_keepalive() if command == "keepalive" else None
                    with self.lock:
                        stats = dict(self.counters)
                    if idle_reply is not None:
                        stats["idle_keepalive_reply"] = idle_reply
                    if command == "stacks":
                        frames = sys._current_frames()
                        stats["thread_stacks"] = [
                            dict(name=thread.name, ident=thread.ident,
                                 stack="".join(traceback.format_stack(frames[thread.ident])))
                            for thread in threading.enumerate() if thread.ident in frames]
                    peer.sendall(json.dumps(stats).encode() + b"\n")
                except OSError:
                    pass

    def ssh_accept(self):
        while True:
            peer, _ = self.ssh_listener.accept()
            with self.lock:
                if self.outage:
                    self.counters["refused_during_outage"] += 1
                    peer.close()
                    continue
            threading.Thread(target=self.ssh, args=(peer,), daemon=True).start()

    def ssh(self, peer):
        transport = FixtureTransport(peer, self)
        with self.lock:
            self.transports.append(transport)
            key = self.keys[self.key_index]
        self.count("connections")
        self.count("active_transports")
        server = Server(self)
        try:
            transport.add_server_key(key)
            transport.start_server(server=server)
            while transport.is_active():
                channel = transport.accept(0.2)
                if channel is None:
                    continue
                destination = server.destinations.pop(channel.get_id(), None)
                if destination is None:
                    # UI-only mode lets Session authenticate and reach its
                    # actual SFTP subsystem request. The transport thread
                    # handles/rejects it while this accept thread waits.
                    rejected = server.sessions.get(channel.get_id())
                    if self.ui_sftp_rejection and rejected is not None:
                        rejected.wait(timeout=3)
                        server.sessions.pop(channel.get_id(), None)
                    channel.close()
                    continue
                with self.lock:
                    self.counters["windows"].append(channel.out_window_size)
                    self.counters["windows"] = self.counters["windows"][-200:]
                threading.Thread(target=self.relay, args=(channel, destination),
                                 name=f"relay-{channel.get_id()}-{destination[1]}", daemon=True).start()
        except (OSError, EOFError, paramiko.SSHException):
            pass
        finally:
            transport.close()
            with self.lock:
                self.transports.remove(transport)
            self.count("active_transports", -1)

    def relay(self, channel, destination):
        self.count("active_relays")
        target = None
        received = sent = 0
        error = ""
        try:
            target = socket.create_connection(destination, timeout=3)
            target.setblocking(False)
            channel.setblocking(False)
            to_target = bytearray()
            to_channel = bytearray()
            channel_eof = target_eof = target_shut = channel_shut = False
            # Paramiko sets closed on SSH_MSG_CHANNEL_CLOSE before its already
            # received data is necessarily drained by this relay thread.
            while True:
                readers = []
                if not channel_eof and len(to_target) < CAP:
                    readers.append(channel)
                if not target_eof and len(to_channel) < CAP:
                    readers.append(target)
                writers = [target] if to_target else []
                if readers or writers:
                    readable, writable, _ = select.select(readers, writers, [], 0.02)
                else:
                    # Winsock rejects select() with three empty descriptor sets.
                    # Only the fixture waits here for Paramiko's SSH window.
                    time.sleep(0.002)
                    readable, writable = [], []
                if channel in readable:
                    try:
                        data = channel.recv(min(CHUNK, CAP - len(to_target)))
                        if data:
                            to_target.extend(data)
                        else:
                            channel_eof = True
                    except (socket.timeout, BlockingIOError):
                        pass
                if target in readable:
                    try:
                        data = target.recv(min(CHUNK, CAP - len(to_channel)))
                        if data:
                            to_channel.extend(data)
                            received += len(data)
                        else:
                            target_eof = True
                    except BlockingIOError:
                        pass
                if target in writable and to_target:
                    try:
                        sent = target.send(to_target)
                        del to_target[:sent]
                    except BlockingIOError:
                        pass
                if to_channel and channel.send_ready():
                    try:
                        sent = channel.send(to_channel[:CHUNK])
                        del to_channel[:sent]
                    except (socket.timeout, BlockingIOError):
                        pass
                with self.lock:
                    self.counters["relay_peak"] = max(self.counters["relay_peak"], len(to_target), len(to_channel))
                if channel_eof and not to_target and not target_shut:
                    target.shutdown(socket.SHUT_WR)
                    target_shut = True
                if target_eof and not to_channel and not channel_shut:
                    channel.shutdown_write()
                    channel_shut = True
                if target_shut and channel_shut:
                    break
                if channel.closed and not channel.recv_ready() and not to_target and not to_channel:
                    break
        except (OSError, EOFError, paramiko.SSHException) as exc:
            error = type(exc).__name__ + ":" + str(exc)
        finally:
            cleanup_errors = []
            try:
                # Attempt both closes even if one fails. In particular Paramiko
                # may raise while closing its pipe or sending SSH channel close.
                for name, resource in (("target", target), ("channel", channel)):
                    if resource is not None:
                        try:
                            resource.close()
                        except Exception as exc:
                            cleanup_errors.append(dict(resource=name, error=type(exc).__name__ + ":" + str(exc),
                                                       traceback=traceback.format_exc()))
                # A relay record now means cleanup has finished, including any
                # failed close attempts; the original transfer error stays separate.
                with self.lock:
                    self.counters["relays"].append(dict(port=destination[1], received=received,
                                                        closed=channel.closed, error=error,
                                                        cleanup_errors=cleanup_errors,
                                                        thread=threading.get_ident()))
                    self.counters["relays"] = self.counters["relays"][-100:]
            finally:
                # Decrement only as this worker exits, never before a blocking
                # close. An exception in cleanup must not leave a stale count.
                self.count("active_relays", -1)


class Server(paramiko.ServerInterface):
    def __init__(self, fixture):
        self.fixture = fixture
        self.destinations = {}
        self.sessions = {}

    def get_allowed_auths(self, username):
        return "password,publickey"

    def check_auth_password(self, username, password):
        if username == "fixture" and password == "fixture-only":
            self.fixture.count("authenticated")
            return paramiko.AUTH_SUCCESSFUL
        self.fixture.count("password_rejected")
        return paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        if username == "fixture" and key == self.fixture.userkey:
            self.fixture.count("key_authenticated")
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_channel_direct_tcpip_request(self, chanid, origin, destination):
        self.fixture.count("direct")
        if destination[0] not in ("127.0.0.1", "::1") or destination[1] not in self.fixture.targets.values() or destination[1] == self.fixture.targets["deny"]:
            self.fixture.count("denied")
            return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
        if destination[1] == self.fixture.targets["delay"]:
            time.sleep(1.0)
        self.destinations[chanid] = destination
        return paramiko.OPEN_SUCCEEDED

    def check_global_request(self, kind, msg):
        if kind == "keepalive@openssh.com":
            self.fixture.count("keepalives")
        return False

    def check_channel_request(self, kind, chanid):
        if self.fixture.ui_sftp_rejection and kind == "session" and len(self.sessions) < 32:
            self.sessions[chanid] = threading.Event()
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_exec_request(self, channel, command):
        self.fixture.count("exec")
        return False

    def check_channel_subsystem_request(self, channel, name):
        self.fixture.count("sftp")
        rejected = self.sessions.get(channel.get_id())
        if rejected is not None:
            rejected.set()
        return False

    def check_channel_pty_request(self, *args):
        self.fixture.count("pty")
        return False

    def check_channel_shell_request(self, channel):
        self.fixture.count("shell")
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ready", type=pathlib.Path, required=True)
    parser.add_argument("--hostkey-type", choices=("rsa", "ecdsa"), default="rsa")
    parser.add_argument("--ui-sftp-rejection", action="store_true",
                        help="Accept session channels only to reject and audit actual SFTP requests")
    args = parser.parse_args()
    fixture = Fixture(args.ready.parent, ui_sftp_rejection=args.ui_sftp_rejection, hostkey_type=args.hostkey_type)
    fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(fixture.keys[0].asbytes()).digest()).decode().rstrip("=")
    info = dict(port=fixture.ssh_listener.getsockname()[1], control=fixture.control.getsockname()[1],
                targets=fixture.targets, fingerprint=fingerprint, key=str(args.ready.parent / "identity"),
                encrypted=str(args.ready.parent / "encrypted"), hostkey_type=fixture.keys[0].get_name(),
                hostkey=fixture.keys[0].get_base64(), alternate_hostkey=fixture.keys[1].get_base64())
    temporary = args.ready.with_suffix(".tmp")
    temporary.write_text(json.dumps(info), encoding="utf-8")
    temporary.replace(args.ready)
    threading.Event().wait()


if __name__ == "__main__":
    main()
