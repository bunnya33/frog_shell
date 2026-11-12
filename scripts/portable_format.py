import ctypes
from ctypes import wintypes
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import struct


MAGIC = b"RSPK0001"
MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
RESERVED = {".complete", ".rs-stage", ".extract.lock"}
APP_HASH = "190196ea8a8673e8fea3cff300a5b845f8d7be5dbf62c626ad120356dd2e6ccc"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def validate_name(name):
    if "\\" in name or not name or len(name.encode("utf-8")) > 4096:
        raise ValueError(f"Invalid payload path: {name!r}")
    for part in name.split("/"):
        stem = part.split(".")[0].upper()
        device = stem in {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}
        device |= len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[-1] in "123456789¹²³"
        if (not part or len(part.encode("utf-16-le")) // 2 > 240 or part[-1] in ". "
                or part.casefold() in RESERVED or device
                or any(ord(character) < 32 or character in '<>:"/\\|?*' for character in part)):
            raise ValueError(f"Unsafe payload component: {part!r}")


class Compression:
    def __init__(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetSystemDirectoryW.argtypes = [wintypes.LPWSTR, wintypes.UINT]
        buffer = ctypes.create_unicode_buffer(32768)
        if not kernel.GetSystemDirectoryW(buffer, len(buffer)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.library = ctypes.WinDLL(str(Path(buffer.value) / "cabinet.dll"), use_last_error=True)
        for prefix in ("Compressor", "Decompressor"):
            create = getattr(self.library, "Create" + prefix)
            create.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
            create.restype = wintypes.BOOL
            close = getattr(self.library, "Close" + prefix)
            close.argtypes = [ctypes.c_void_p]
            close.restype = wintypes.BOOL
        for name in ("Compress", "Decompress"):
            method = getattr(self.library, name)
            method.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                               ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
            method.restype = wintypes.BOOL

    def transform(self, data, expected=None):
        compressing = expected is None
        suffix = "Compressor" if compressing else "Decompressor"
        create = getattr(self.library, "Create" + suffix)
        close = getattr(self.library, "Close" + suffix)
        method = self.library.Compress if compressing else self.library.Decompress
        handle = ctypes.c_void_p()
        if not create(4, None, ctypes.byref(handle)):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            size = ctypes.c_size_t(expected or 0)
            if compressing:
                if method(handle, data, len(data), None, 0, ctypes.byref(size)):
                    raise ValueError("Unexpected compression size query result")
                if ctypes.get_last_error() != 122:
                    raise ctypes.WinError(ctypes.get_last_error())
            if not 0 < size.value <= MAX_FILE:
                raise ValueError("Compression buffer exceeds bound")
            output = ctypes.create_string_buffer(size.value)
            if not method(handle, data, len(data), output, len(output), ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            if expected is not None and size.value != expected:
                raise ValueError("Decompression length mismatch")
            return output.raw[:size.value]
        finally:
            close(handle)


def source_inventory(source):
    source = Path(source).absolute()
    items = []
    for path in sorted(source.rglob("*"), key=lambda item: item.relative_to(source).as_posix().encode("utf-8")):
        stat = path.lstat()
        if getattr(stat, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Source reparse point is not allowed: {path}")
        if path.is_dir():
            continue
        name = path.relative_to(source).as_posix()
        validate_name(name)
        if not path.is_file() or stat.st_nlink != 1 or stat.st_size > MAX_FILE:
            raise ValueError(f"Unsupported source file: {path}")
        items.append({"path": name, "size": stat.st_size, "sha256": sha256(path.read_bytes())})
    names = {item["path"].casefold() for item in items}
    if not 0 < len(items) <= 1024 or len(names) != len(items):
        raise ValueError("Invalid file count or case-insensitive collision")
    for name in names:
        if any(str(parent).casefold() in names for parent in PurePosixPath(name).parents if str(parent) != "."):
            raise ValueError("File/directory collision")
    if sum(item["size"] for item in items) > MAX_TOTAL:
        raise ValueError("Payload total exceeds bound")
    return items


def make_icon():
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = []
    for pixels in sizes:
        bitmap = bytearray()
        for row in range(pixels - 1, -1, -1):
            for column in range(pixels):
                red = green = blue = alpha = 0.0
                for vertical in range(4):
                    for horizontal in range(4):
                        point_x = (column + (horizontal + 0.5) / 4) * 32 / pixels
                        point_y = (row + (vertical + 0.5) / 4) * 32 / pixels
                        nearest_x = min(24, max(8, point_x))
                        nearest_y = min(24, max(8, point_y))
                        if (point_x - nearest_x) ** 2 + (point_y - nearest_y) ** 2 > 49:
                            continue
                        color = (111, 66, 168)
                        for start_x, start_y, end_x, end_y in ((8, 12, 13, 17), (13, 17, 8, 22), (18, 22, 24, 22)):
                            delta_x, delta_y = end_x - start_x, end_y - start_y
                            fraction = min(1, max(0, ((point_x - start_x) * delta_x + (point_y - start_y) * delta_y) / (delta_x ** 2 + delta_y ** 2)))
                            distance = math.hypot(point_x - start_x - fraction * delta_x, point_y - start_y - fraction * delta_y)
                            if distance <= 1.15:
                                color = (248, 248, 242)
                        red += color[0]; green += color[1]; blue += color[2]; alpha += 1
                bitmap.extend((round(blue / alpha) if alpha else 0, round(green / alpha) if alpha else 0,
                               round(red / alpha) if alpha else 0, round(alpha * 255 / 16)))
        mask = bytes(((pixels + 31) // 32) * 4 * pixels)
        dib = struct.pack("<IiiHHIIiiII", 40, pixels, pixels * 2, 1, 32, 0, len(bitmap), 0, 0, 0, 0)
        images.append(dib + bitmap + mask)
    result = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for pixels, image in zip(sizes, images):
        result.extend(struct.pack("<BBBBHHII", pixels % 256, pixels % 256, 0, 0, 1, 32, len(image), offset))
        offset += len(image)
    return bytes(result) + b"".join(images)


def generate(source, destination, release=True, version="0.1.12", manifest=None):
    source, destination = Path(source).absolute(), Path(destination).absolute()
    items = source_inventory(source)
    applications = [item for item in items if item["path"] in ("FrogShell.exe", "RabbitShell.exe")]
    if len(applications) != 1:
        raise ValueError("Payload must contain exactly one supported main executable")
    app = applications[0]
    if release and manifest is not None:
        approved = json.loads(Path(manifest).read_text(encoding="utf-8-sig"))
        if approved["version"] != version or approved["files"] != items:
            raise ValueError("Source does not match the supplied release manifest")
    elif release and version != "0.1.12":
        raise ValueError("A pinned inventory manifest is required for a new release")
    elif release and (app["path"] != "RabbitShell.exe" or len(items) != 62 or sum(item["size"] for item in items) != 34584228 or app["sha256"] != APP_HASH):
        raise ValueError("The immutable 0.1.12 directory package does not match the approved baseline")
    destination.mkdir(parents=True, exist_ok=True)
    compressor = Compression()
    table = bytearray()
    data_path = destination / "payload-data.tmp"
    with data_path.open("wb") as stream:
        for item in items:
            data = (source / item["path"]).read_bytes()
            if len(data) != item["size"] or sha256(data) != item["sha256"]:
                raise ValueError("Source changed while packing")
            packed = compressor.transform(data) if data else data
            algorithm = 4 if len(packed) < len(data) else 0
            stored = packed if algorithm else data
            name = item["path"].encode("utf-8")
            table.extend(struct.pack("<IIQQ", len(name), algorithm, len(data), len(stored)))
            table.extend(bytes.fromhex(item["sha256"]) + name)
            item.update(algorithm=algorithm, stored_size=len(stored))
            stream.write(stored)
    if len(table) > 1024 * 1024:
        raise ValueError("Manifest exceeds bound")
    archive_path = destination / "payload.bin"
    with archive_path.open("wb") as stream, data_path.open("rb") as contents:
        stream.write(struct.pack("<8sII", MAGIC, len(items), len(table)))
        stream.write(table)
        while chunk := contents.read(65536):
            stream.write(chunk)
    data_path.unlink()
    digest = sha256(archive_path.read_bytes())
    metadata = {"format": "RSPK0001", "compression": "Windows XPRESS_HUFF (4), per file or stored",
                "version": version, "main_executable": app["path"], "display_name": app["path"][:-4], "payload_sha256": digest, "payload_size": archive_path.stat().st_size,
                "file_count": len(items), "unpacked_size": sum(item["size"] for item in items), "files": items}
    (destination / "payload-manifest.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (destination / "payload_meta.h").write_text(
        '#pragma once\ninline constexpr char kPayloadHash[] = "' + digest + '";\n'
        + f'inline constexpr unsigned long kPayloadSize = {archive_path.stat().st_size}UL;\n'
        + f'#define PORTABLE_MAIN_EXECUTABLE L"{app["path"]}"\n'
        + f'#define PORTABLE_DISPLAY_NAME L"{app["path"][:-4]}"\n', encoding="ascii", newline="\n")
    icon = Path(__file__).resolve().parents[1] / "resources/branding/frogshell.ico"
    (destination / "RabbitShell.ico").write_bytes(icon.read_bytes() if app["path"] == "FrogShell.exe" else make_icon())
    return metadata


def parse_archive(data):
    magic, count, table_size = struct.unpack_from("<8sII", data)
    if magic != MAGIC or not 0 < count <= 1024 or table_size > 1024 * 1024 or 16 + table_size > len(data):
        raise ValueError("Invalid archive header")
    offset, body_offset = 16, 16 + table_size
    items = []
    for unused in range(count):
        if offset + 56 > 16 + table_size:
            raise ValueError("Truncated manifest")
        length, algorithm, size, stored = struct.unpack_from("<IIQQ", data, offset)
        digest = data[offset + 24:offset + 56].hex()
        offset += 56
        if (not 0 < length <= 4096 or offset + length > 16 + table_size or algorithm not in (0, 4)
                or size > MAX_FILE or stored > MAX_FILE or body_offset + stored > len(data)
                or algorithm == 0 and size != stored):
            raise ValueError("Invalid manifest record")
        name = data[offset:offset + length].decode("utf-8", errors="strict")
        validate_name(name)
        items.append({"path": name, "size": size, "sha256": digest, "algorithm": algorithm,
                      "stored_size": stored, "offset": body_offset})
        offset += length
        body_offset += stored
    if offset != 16 + table_size or body_offset != len(data) or sum(item["size"] for item in items) > MAX_TOTAL:
        raise ValueError("Invalid archive boundaries")
    return items


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.pe = struct.unpack_from("<I", self.data, 0x3c)[0]
        if self.data[:2] != b"MZ" or self.data[self.pe:self.pe + 4] != b"PE\0\0":
            raise ValueError("Not a PE executable")
        self.machine, sections, unused, unused, unused, optional_size, self.characteristics = struct.unpack_from("<HHIIIHH", self.data, self.pe + 4)
        self.optional = self.pe + 24
        if struct.unpack_from("<H", self.data, self.optional)[0] != 0x20b:
            raise ValueError("Not PE32+")
        self.sections = []
        for index in range(sections):
            base = self.optional + optional_size + index * 40
            virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from("<IIII", self.data, base + 8)
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_address))

    def offset(self, rva):
        for virtual_address, size, raw_address in self.sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_address + rva - virtual_address
        raise ValueError(f"Unmapped RVA: {rva:x}")

    def directory(self, index):
        return struct.unpack_from("<II", self.data, self.optional + 112 + index * 8)

    def resource(self, *identifiers):
        root = self.offset(self.directory(2)[0])
        directory = root
        for identifier in identifiers:
            named, numeric = struct.unpack_from("<HH", self.data, directory + 12)
            records = [struct.unpack_from("<II", self.data, directory + 16 + index * 8) for index in range(named + numeric)]
            candidates = [value for name, value in records if name == identifier] if identifier is not None else [value for name, value in records]
            if len(candidates) != 1:
                raise ValueError("Missing or ambiguous PE resource")
            value = candidates[0]
            if value & 0x80000000:
                directory = root + (value & 0x7fffffff)
            else:
                address, size = struct.unpack_from("<II", self.data, root + value)
                return self.data[self.offset(address):self.offset(address) + size]
        raise ValueError("Resource path is incomplete")

    def imports(self):
        rva, size = self.directory(1)
        if not rva:
            return []
        result = []
        cursor = self.offset(rva)
        for unused in range(size // 20):
            record = struct.unpack_from("<IIIII", self.data, cursor)
            if not any(record):
                break
            start = self.offset(record[3])
            result.append(self.data[start:self.data.index(b"\0", start)].decode("ascii"))
            cursor += 20
        return result
