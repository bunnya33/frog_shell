import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import uuid

from portable_format import sha256


KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
HANDLE = wintypes.HANDLE
SIZE = ctypes.c_size_t
INVALID = ctypes.c_void_p(-1).value


class Startup(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("reserved", wintypes.LPWSTR), ("desktop", wintypes.LPWSTR),
                ("title", wintypes.LPWSTR), ("x", wintypes.DWORD), ("y", wintypes.DWORD),
                ("width", wintypes.DWORD), ("height", wintypes.DWORD), ("chars_x", wintypes.DWORD),
                ("chars_y", wintypes.DWORD), ("fill", wintypes.DWORD), ("flags", wintypes.DWORD),
                ("show", wintypes.WORD), ("reserved_size", wintypes.WORD), ("reserved_data", ctypes.c_void_p),
                ("stdin", HANDLE), ("stdout", HANDLE), ("stderr", HANDLE)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [("process", HANDLE), ("thread", HANDLE), ("pid", wintypes.DWORD), ("tid", wintypes.DWORD)]


class BasicLimits(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64), ("flags", wintypes.DWORD),
                ("min_working_set", SIZE), ("max_working_set", SIZE), ("active_limit", wintypes.DWORD),
                ("affinity", SIZE), ("priority", wintypes.DWORD), ("scheduling", wintypes.DWORD)]


class Limits(ctypes.Structure):
    _fields_ = [("basic", BasicLimits), ("io", ctypes.c_uint64 * 6), ("process_memory", SIZE),
                ("job_memory", SIZE), ("peak_process", SIZE), ("peak_job", SIZE)]


class Memory(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD), ("peak_working_set", SIZE),
                ("working_set", SIZE), ("peak_paged", SIZE), ("paged", SIZE), ("peak_nonpaged", SIZE),
                ("nonpaged", SIZE), ("pagefile", SIZE), ("peak_pagefile", SIZE), ("private", SIZE)]


def declare(name, arguments, result=wintypes.BOOL):
    function = getattr(KERNEL, name)
    function.argtypes, function.restype = arguments, result
    return function


declare("CreateJobObjectW", [ctypes.c_void_p, wintypes.LPCWSTR], HANDLE)
declare("SetInformationJobObject", [HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD])
declare("QueryInformationJobObject", [HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p])
declare("AssignProcessToJobObject", [HANDLE, HANDLE])
declare("IsProcessInJob", [HANDLE, HANDLE, ctypes.POINTER(wintypes.BOOL)])
declare("CreateProcessW", [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
                           wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
                           ctypes.POINTER(Startup), ctypes.POINTER(ProcessInfo)])
declare("ResumeThread", [HANDLE], wintypes.DWORD)
declare("WaitForSingleObject", [HANDLE, wintypes.DWORD], wintypes.DWORD)
declare("GetExitCodeProcess", [HANDLE, ctypes.POINTER(wintypes.DWORD)])
declare("TerminateProcess", [HANDLE, wintypes.UINT])
declare("TerminateJobObject", [HANDLE, wintypes.UINT])
declare("CloseHandle", [HANDLE])
declare("OpenProcess", [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], HANDLE)
declare("QueryFullProcessImageNameW", [HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)])
declare("GetProcessHandleCount", [HANDLE, ctypes.POINTER(wintypes.DWORD)])
declare("K32GetProcessMemoryInfo", [HANDLE, ctypes.POINTER(Memory), wintypes.DWORD])
declare("K32EnumProcessModules", [HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)])
declare("K32GetModuleFileNameExW", [HANDLE, HANDLE, wintypes.LPWSTR, wintypes.DWORD], wintypes.DWORD)
declare("CreateFileW", [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                        wintypes.DWORD, wintypes.DWORD, HANDLE], HANDLE)
declare("DeviceIoControl", [HANDLE, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                            ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p])


def checked(value):
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value


def system_directory():
    buffer = ctypes.create_unicode_buffer(32768)
    declare("GetSystemDirectoryW", [wintypes.LPWSTR, wintypes.UINT], wintypes.UINT)
    checked(KERNEL.GetSystemDirectoryW(buffer, len(buffer)))
    return Path(buffer.value)


class Sandbox:
    def __init__(self, root, name, executable, metadata):
        self.root = root / (name + " 中文 空间" if "unicode" in name else name)
        self.root.mkdir(parents=True)
        self.download = self.root / "空白 下载目录" if "unicode" in name else self.root / "empty-download"
        self.download.mkdir()
        self.exe = self.download / executable.name
        shutil.copyfile(executable, self.exe)
        assert list(self.download.iterdir()) == [self.exe]
        self.local = self.root / "profile/Local"
        self.cache = self.local / "RabbitShell/runtime" / metadata["payload_sha256"]
        self.metadata = metadata
        system = system_directory()
        profile = self.root / "profile"
        self.data = self.root / "独立 数据目录"
        directories = [self.local, profile / "Roaming", profile / "Temp", self.data, self.root / "ProgramData"]
        for directory in directories:
            directory.mkdir(parents=True, exist_ok=True)
        (self.data / "connections.json").write_text("[]\n", encoding="utf-8")
        self.env = {"SystemRoot": str(system.parent), "WINDIR": str(system.parent), "PATH": str(system),
                    "COMSPEC": str(system / "cmd.exe"), "USERPROFILE": str(profile), "HOME": str(profile),
                    "HOMEDRIVE": profile.drive, "HOMEPATH": str(profile)[len(profile.drive):],
                    "LOCALAPPDATA": str(self.local), "APPDATA": str(profile / "Roaming"),
                    "TEMP": str(profile / "Temp"), "TMP": str(profile / "Temp"),
                    "PROGRAMDATA": str(self.root / "ProgramData"), "ALLUSERSPROFILE": str(self.root / "ProgramData"),
                    "RABBITSHELL_DATA_DIR": str(self.data), "RABBITSHELL_PORTABLE_NO_DIALOG": "1",
                    "QT_QPA_PLATFORM": "offscreen"}

    def args(self, *extra):
        return ["--data-dir", str(self.data), *extra]

    def validate_cache(self):
        for item in self.metadata["files"]:
            path = Path("\\\\?\\" + str(self.cache / item["path"]))
            assert not path.is_symlink() and path.stat().st_nlink == 1
            assert path.stat().st_size == item["size"] and sha256(path.read_bytes()) == item["sha256"], str(path)
        assert Path("\\\\?\\" + str(self.cache / ".complete")).read_text(encoding="ascii") == self.metadata["payload_sha256"] + "\n"


class OwnedProcess:
    def __init__(self, sandbox, name, arguments, raw_tail=None, extra_env=None):
        import msvcrt
        self.sandbox, self.name = sandbox, name
        self.started = time.perf_counter()
        self.first_marker_ms = self.first_child_ms = None
        self.job = self.process = None
        self.pid = None
        self.observed = []
        self.logs = [sandbox.root / (name + ".stdout.txt"), sandbox.root / (name + ".stderr.txt")]
        self.job = checked(KERNEL.CreateJobObjectW(None, None))
        limits = Limits()
        limits.basic.flags = 0x2000
        try:
            checked(KERNEL.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
            self.raw_tail = raw_tail if raw_tail is not None else " " + subprocess.list2cmdline(arguments)
            command = ctypes.create_unicode_buffer('"' + str(sandbox.exe) + '"' + self.raw_tail)
            env = dict(sandbox.env)
            env.update(extra_env or {})
            block = ctypes.create_unicode_buffer("\0".join(key + "=" + value for key, value in sorted(env.items(), key=lambda pair: pair[0].upper())) + "\0\0")
            startup = Startup()
            startup.cb, startup.flags, startup.show = ctypes.sizeof(startup), 0x101, 0
            process = ProcessInfo()
            with open(os.devnull, "rb") as stdin, self.logs[0].open("wb") as stdout, self.logs[1].open("wb") as stderr:
                handles = [msvcrt.get_osfhandle(stream.fileno()) for stream in (stdin, stdout, stderr)]
                for handle in handles:
                    os.set_handle_inheritable(handle, True)
                startup.stdin, startup.stdout, startup.stderr = handles
                checked(KERNEL.CreateProcessW(str(sandbox.exe), command, None, None, True,
                                               0x4 | 0x400 | 0x08000000, block, str(sandbox.download),
                                               ctypes.byref(startup), ctypes.byref(process)))
                self.process, self.pid = process.process, process.pid
                try:
                    checked(KERNEL.AssignProcessToJobObject(self.job, self.process))
                    if KERNEL.ResumeThread(process.thread) == 0xffffffff:
                        raise ctypes.WinError(ctypes.get_last_error())
                finally:
                    KERNEL.CloseHandle(process.thread)
        except BaseException:
            if self.process:
                KERNEL.TerminateProcess(self.process, 99)
            self.close()
            raise

    def pids(self):
        buffer = ctypes.create_string_buffer(8 + 128 * ctypes.sizeof(SIZE))
        checked(KERNEL.QueryInformationJobObject(self.job, 3, buffer, len(buffer), None))
        assigned, count = struct.unpack_from("<II", buffer)
        assert count <= 128 and assigned >= count
        return list((SIZE * count).from_buffer_copy(buffer, 8))

    def open_member(self, pid):
        handle = KERNEL.OpenProcess(0x1000 | 0x10 | 0x400 | 0x1 | 0x100000, False, pid)
        if not handle:
            return None
        member = wintypes.BOOL()
        if not KERNEL.IsProcessInJob(handle, self.job, ctypes.byref(member)) or not member.value:
            KERNEL.CloseHandle(handle)
            return None
        return handle

    def sample(self):
        elapsed = (time.perf_counter() - self.started) * 1000
        if self.first_marker_ms is None and (self.sandbox.cache / ".complete").exists():
            self.first_marker_ms = elapsed
        results = []
        for pid in self.pids():
            handle = self.open_member(pid)
            if not handle:
                continue
            try:
                name = ctypes.create_unicode_buffer(32768)
                size = wintypes.DWORD(len(name))
                checked(KERNEL.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)))
                image_path = name.value
                memory, count = Memory(), wintypes.DWORD()
                memory.cb = ctypes.sizeof(memory)
                checked(KERNEL.K32GetProcessMemoryInfo(handle, ctypes.byref(memory), ctypes.sizeof(memory)))
                checked(KERNEL.GetProcessHandleCount(handle, ctypes.byref(count)))
                modules, needed = (HANDLE * 512)(), wintypes.DWORD()
                module_paths = []
                if KERNEL.K32EnumProcessModules(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed)):
                    for module in list(modules)[:min(512, needed.value // ctypes.sizeof(HANDLE))]:
                        if KERNEL.K32GetModuleFileNameExW(handle, module, name, len(name)):
                            module_paths.append(name.value)
                result = {"pid": pid, "image": image_path, "working_set": memory.working_set,
                          "private_bytes": memory.private, "peak_working_set": memory.peak_working_set,
                          "handles": count.value, "modules": module_paths}
                if pid != self.pid and self.first_child_ms is None:
                    self.first_child_ms = elapsed
                results.append(result)
            except OSError:
                if KERNEL.WaitForSingleObject(handle, 50) != 0:
                    raise
            finally:
                KERNEL.CloseHandle(handle)
        self.observed = results
        return results

    def wait(self, timeout=90):
        deadline = time.perf_counter() + timeout
        while KERNEL.WaitForSingleObject(self.process, 20) == 258:
            self.sample()
            if time.perf_counter() >= deadline:
                raise TimeoutError(self.name)
        code = wintypes.DWORD()
        checked(KERNEL.GetExitCodeProcess(self.process, ctypes.byref(code)))
        deadline = time.perf_counter() + 5
        while self.pids() and time.perf_counter() < deadline:
            time.sleep(0.02)
        assert not self.pids(), "Job retains a process after launcher exit"
        accounting = ctypes.create_string_buffer(48)
        checked(KERNEL.QueryInformationJobObject(self.job, 1, accounting, len(accounting), None))
        total_processes = struct.unpack_from("<I", accounting, 36)[0]
        download_unchanged = list(self.sandbox.download.iterdir()) == [self.sandbox.exe]
        if code.value != 125:
            assert download_unchanged, "Successful run wrote outside the managed runtime into the download directory"
        return {"name": self.name, "exit_code": code.value, "elapsed_ms": (time.perf_counter() - self.started) * 1000,
                "cache_path": str(self.sandbox.cache), "single_exe_directory": str(self.sandbox.download),
                "single_exe_directory_unchanged": download_unchanged,
                "first_marker_observed_ms": self.first_marker_ms, "first_child_observed_ms": self.first_child_ms,
                "stdout": self.logs[0].read_text(encoding="utf-8", errors="replace"),
                "stderr": self.logs[1].read_text(encoding="utf-8", errors="replace"),
                "last_sample": self.observed, "remaining_job_processes": 0, "total_job_processes": total_processes}

    def stop_child(self, code=73):
        children = [pid for pid in self.pids() if pid != self.pid]
        assert len(children) == 1, children
        handle = self.open_member(children[0])
        assert handle
        try:
            checked(KERNEL.TerminateProcess(handle, code))
        finally:
            KERNEL.CloseHandle(handle)

    def close(self):
        if self.job:
            KERNEL.TerminateJobObject(self.job, 99)
        if self.process:
            KERNEL.WaitForSingleObject(self.process, 5000)
            KERNEL.CloseHandle(self.process)
            self.process = None
        if self.job:
            KERNEL.CloseHandle(self.job)
            self.job = None

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


def junction(link, target):
    link.mkdir(parents=True)
    substitute = ("\\??\\" + str(target.absolute())).encode("utf-16-le")
    printable = str(target.absolute()).encode("utf-16-le")
    names = substitute + b"\0\0" + printable + b"\0\0"
    data = struct.pack("<IHHHHHH", 0xa0000003, len(names) + 8, 0, 0, len(substitute), len(substitute) + 2, len(printable)) + names
    handle = KERNEL.CreateFileW(str(link), 0x40000000, 7, None, 3, 0x02200000, None)
    if handle == INVALID:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        returned = wintypes.DWORD()
        checked(KERNEL.DeviceIoControl(handle, 0x900a4, data, len(data), None, 0, ctypes.byref(returned), None))
    finally:
        KERNEL.CloseHandle(handle)


def snapshot(directory):
    return {path.relative_to(directory).as_posix(): (path.stat().st_size, path.stat().st_mtime_ns, sha256(path.read_bytes()))
            for path in directory.rglob("*") if path.is_file()}


def decode_probe(path):
    data, result, offset = path.read_bytes(), [], 0
    while offset < len(data):
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        result.append(data[offset:offset + size].decode("utf-16-le"))
        offset += size
    assert offset == len(data)
    return result


def run_suite(executable, source, report, metadata):
    root = report / ("isolated-" + time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
    root.mkdir(parents=True)
    results = {"scope": "wrapper only; no SSH/UI interaction; no real profiles or credentials; private Job objects only",
               "run_root": str(root), "status": "RUNNING", "cases": [], "runtime_cache": [],
               "latency_scope": "wall-clock, includes Qt process startup and polling; not pure decompression CPU time",
               "cleanup": "Evidence retained. No recursive deletion, including reparse fixtures."}
    cases = results["cases"]

    def version(box, name):
        with OwnedProcess(box, name, box.args("--version")) as process:
            result = process.wait()
            assert result["exit_code"] == 0 and "0.1.12" in result["stdout"], result
            cases.append(result)
        box.validate_cache()

    def failure(box, name):
        with OwnedProcess(box, name, box.args("--version")) as process:
            result = process.wait(timeout=15)
            assert result["exit_code"] == 125 and "code:" in result["stderr"], result
            assert result["total_job_processes"] == 1, "Failed preparation must not launch a child"
            assert all(item["pid"] == process.pid for item in result["last_sample"]), result
            cases.append(result)

    try:
        box = Sandbox(root, "first-ascii", executable, metadata)
        results["runtime_cache"].append(str(box.cache))
        other = box.cache.parent / "other-payload"
        other.mkdir(parents=True)
        (other / "sentinel.txt").write_bytes(b"do not alter another payload")
        sentinel = snapshot(other)
        version(box, "first-extract")
        before = snapshot(box.cache)
        version(box, "verified-reuse")
        assert snapshot(box.cache) == before
        unicode_box = Sandbox(root, "unicode-path", executable, metadata)
        version(unicode_box, "unicode-extract")
        (box.cache / "RabbitShell.exe").write_bytes(b"damaged executable; must never run")
        (box.cache / "Qt6Core.dll").write_bytes(b"damaged Qt DLL")
        (box.cache / "README.md").unlink()
        (box.cache / "platforms/qoffscreen.dll").unlink()
        (box.cache / ".complete").write_bytes(b"invalid complete marker")
        version(box, "corrupt-and-missing-recovery")
        assert snapshot(other) == sentinel
        partial = Sandbox(root, "incomplete-cache", executable, metadata)
        partial.cache.mkdir(parents=True)
        (partial.cache / ".rs-stage").write_bytes(b"interrupted extraction")
        (partial.cache / "RabbitShell.exe").write_bytes(b"partial")
        version(partial, "interrupted-extract-recovery")
        unknown = box.cache / "unknown-user-file.txt"
        unknown.write_bytes(b"must not delete unknown content")
        failure(box, "unknown-content-fail-closed")
        assert unknown.read_bytes() == b"must not delete unknown content"
        unknown.unlink()
        readonly = box.cache / "README.md"
        readonly.write_bytes(b"corrupt read-only owned file")
        os.chmod(readonly, 0o444)
        try:
            failure(box, "unrecoverable-readonly-no-launch")
        finally:
            os.chmod(readonly, 0o666)
        version(box, "recover-after-readonly")
        concurrent = Sandbox(root, "concurrent-fresh", executable, metadata)
        processes = []
        try:
            for index in range(4):
                processes.append(OwnedProcess(concurrent, f"parallel-{index}", concurrent.args("--version")))
            for process in processes:
                result = process.wait()
                assert result["exit_code"] == 0 and "0.1.12" in result["stdout"], result
                cases.append(result)
        finally:
            for process in processes:
                process.close()
        concurrent.validate_cache()
        active = []
        alive_samples = {}
        try:
            for mode in ("manager", "demo"):
                active.append(OwnedProcess(box, mode, box.args(*(["--demo"] if mode == "demo" else []))))
            deadline = time.perf_counter() + 60
            while True:
                ready = []
                for process in active:
                    samples = process.sample()
                    children = [item for item in samples if item["pid"] != process.pid]
                    ready.append(bool(children) and any(Path(module).name.lower() == "qt6widgets.dll" for module in children[0]["modules"]))
                if all(ready):
                    break
                assert time.perf_counter() < deadline, "Manager/demo did not become ready concurrently"
                time.sleep(0.05)
            time.sleep(1)
            for process in active:
                samples = process.sample()
                alive_samples[process.name] = samples
                for child in [item for item in samples if item["pid"] != process.pid]:
                    assert Path(child["image"]).resolve() == (box.cache / "RabbitShell.exe").resolve()
                    for module in child["modules"]:
                        if Path(module).name.lower().startswith("qt6") or Path(module).name.lower() == "qoffscreen.dll":
                            assert Path(module.removeprefix("\\\\?\\")).is_relative_to(box.cache), module
                process.stop_child()
            for process in active:
                result = process.wait()
                assert result["exit_code"] == 73, result
                result["alive_samples"] = alive_samples[process.name]
                result["exit_scope"] = "Test terminated only its own child; launcher forwarded code 73. Not graceful UI shutdown."
                cases.append(result)
        finally:
            for process in active:
                process.close()
        for relative in ("RabbitShell", "RabbitShell/runtime", "RabbitShell/runtime/" + metadata["payload_sha256"],
                         "RabbitShell/runtime/" + metadata["payload_sha256"] + "/platforms",
                         "RabbitShell/runtime/" + metadata["payload_sha256"] + "/RabbitShell.exe",
                         "RabbitShell/runtime/" + metadata["payload_sha256"] + "/.rs-stage",
                         "RabbitShell/runtime/" + metadata["payload_sha256"] + "/.complete",
                         "RabbitShell/runtime/" + metadata["payload_sha256"] + "/.extract.lock"):
            linked = Sandbox(root, "junction-" + str(len(cases)), executable, metadata)
            target = linked.root / "outside-managed-cache"
            target.mkdir()
            (target / "sentinel.txt").write_bytes(b"sentinel must not change")
            unchanged = snapshot(target)
            junction(linked.local / relative, target)
            failure(linked, "reject-junction")
            assert snapshot(target) == unchanged
            cases[-1]["junction"] = relative
        linked = Sandbox(root, "hardlink-target", executable, metadata)
        linked.cache.mkdir(parents=True)
        target = linked.root / "sentinel.bin"
        target.write_bytes(b"hardlink sentinel")
        os.link(target, linked.cache / "RabbitShell.exe")
        failure(linked, "reject-hardlink")
        assert target.read_bytes() == b"hardlink sentinel"
        linked = Sandbox(root, "symlink-target", executable, metadata)
        linked.cache.mkdir(parents=True)
        target = linked.root / "sentinel.bin"
        target.write_bytes(b"symlink sentinel")
        try:
            os.symlink(target, linked.cache / "RabbitShell.exe")
        except OSError as error:
            cases.append({"name": "reject-file-symlink", "status": "SKIP", "reason": str(error)})
        else:
            failure(linked, "reject-file-symlink")
            assert target.read_bytes() == b"symlink sentinel"
        build = Path(__file__).resolve().parent.parent / "build/portable-0.1.12"
        fixture_exe = build / "probe-cmake/Release/RabbitShell-portable-probe.exe"
        fixture_meta = json.loads((build / "probe-generated/payload-manifest.json").read_text(encoding="utf-8"))
        probe = Sandbox(root, "unicode-probe", fixture_exe, fixture_meta)
        output = probe.root / "probe.bin"
        arguments = ["--data-dir", "中文 path ending\\", "--demo", "--version", "", 'embedded"quote',
                     'slash\\\\"quote', "C:\\ends with spaces\\\\", "tab\tvalue", "x" * 2048]
        tail = "  \t" + subprocess.list2cmdline(arguments)
        with OwnedProcess(probe, "arguments-raw-tail", [], raw_tail=tail,
                          extra_env={"RABBITSHELL_PROBE_REPORT": str(output), "QT_PLUGIN_PATH": str(probe.download)}) as process:
            result = process.wait()
            assert result["exit_code"] == 37
            assert "portable probe stdout" in result["stdout"] and "portable probe stderr" in result["stderr"]
            raw, cwd, path, plugin_path, *argv = decode_probe(output)
            assert argv[1:] == arguments, argv
            assert raw == '"' + str(probe.cache / "RabbitShell.exe") + '"' + tail, raw
            assert Path(cwd) == probe.cache
            assert path == str(probe.cache) + ";" + str(system_directory()) and plugin_path == ""
            result["assertions"] = ["exact raw argument tail", "Unicode/space/quotes/backslashes/empty/tab/long args", "cwd", "sanitized PATH/QT_PLUGIN_PATH", "stdout/stderr", "exit code 37"]
            cases.append(result)
        probe.validate_cache()
        developer = probe.root / "developer-runtime"
        shutil.copytree(build / "probe-source", developer)
        (developer / "replaceable.dll").write_bytes(b"legal replacement fixture; never dynamically loaded")
        with OwnedProcess(probe, "developer-dll-override", [], extra_env={"RABBITSHELL_PORTABLE_RUNTIME": str(developer),
                                                                                     "RABBITSHELL_PROBE_REPORT": str(output)}) as process:
            result = process.wait()
            assert result["exit_code"] == 37
            assert Path(decode_probe(output)[1]) == developer
            cases.append(result)
        (developer / "docs/参数说明.txt").write_bytes(b"unapproved non-DLL change")
        with OwnedProcess(probe, "developer-non-dll-rejected", [], extra_env={"RABBITSHELL_PORTABLE_RUNTIME": str(developer),
                                                                               "RABBITSHELL_PROBE_REPORT": str(output)}) as process:
            result = process.wait()
            assert result["exit_code"] == 125 and result["total_job_processes"] == 1
            cases.append(result)
        results["status"] = "PASS" if not any(item.get("status") == "SKIP" for item in cases) else "PASS WITH EXPLICIT SKIPS"
        results["first_vs_reuse_wall_clock_ms"] = {"first": cases[0]["elapsed_ms"], "reuse": cases[1]["elapsed_ms"],
                                                       "difference_not_pure_extraction": cases[0]["elapsed_ms"] - cases[1]["elapsed_ms"]}
    except BaseException as error:
        results["status"] = "FAIL"
        results["error"] = repr(error)
        raise
    finally:
        (root / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("Isolated wrapper report: " + str(root / "results.json"))
