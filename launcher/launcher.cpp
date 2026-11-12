#include <windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "payload_meta.h"
#ifndef PORTABLE_MAIN_EXECUTABLE
#define PORTABLE_MAIN_EXECUTABLE L"RabbitShell.exe"
#endif
#ifndef PORTABLE_DISPLAY_NAME
#define PORTABLE_DISPLAY_NAME L"RabbitShell"
#endif

namespace {
using Bytes = std::vector<unsigned char>;
using Digest = std::array<unsigned char, 32>;
constexpr DWORD kFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN;
constexpr std::uint64_t kMaxFile = 64ull * 1024 * 1024;
constexpr std::uint64_t kMaxTotal = 256ull * 1024 * 1024;
constexpr wchar_t kStage[] = L".rs-stage";
constexpr wchar_t kMarker[] = L".complete";
constexpr wchar_t kLock[] = L".extract.lock";

struct Failure { std::wstring message; DWORD code; };
[[noreturn]] void fail(const std::wstring &message, DWORD code = GetLastError()) {
    throw Failure{message, code};
}
void require(bool condition, const std::wstring &message) {
    if (!condition) fail(message, ERROR_INVALID_DATA);
}
class Handle {
    HANDLE value_ = INVALID_HANDLE_VALUE;
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other) { if (valid()) CloseHandle(value_); value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE); }
        return *this;
    }
    bool valid() const { return value_ != INVALID_HANDLE_VALUE && value_ != nullptr; }
    HANDLE get() const { return value_; }
};
class Library {
    HMODULE value_;
public:
    explicit Library(const wchar_t *name) : value_(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!value_) fail(L"无法载入系统组件");
    }
    ~Library() { FreeLibrary(value_); }
    template<class Function> Function function(const char *name) const {
        const auto address = GetProcAddress(value_, name);
        if (!address) fail(L"系统组件缺少必要接口");
        Function result;
        static_assert(sizeof(result) == sizeof(address));
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
};
std::wstring environment(const wchar_t *name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    require(size <= 32767, L"环境变量过长");
    std::wstring result(size, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, result.data(), size);
    if (!length || length >= size) fail(L"读取环境变量失败");
    result.resize(length);
    return result;
}
std::wstring utf16(const std::string &text) {
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    require(length > 0, L"无效的 UTF-8 路径");
    std::wstring result(length, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) == length, L"路径解码失败");
    return result;
}
std::string hex(const Digest &digest) {
    std::string result;
    for (const auto value : digest) { result += "0123456789abcdef"[value >> 4]; result += "0123456789abcdef"[value & 15]; }
    return result;
}
class Hasher {
    Library library_{L"bcrypt.dll"};
    decltype(&BCryptOpenAlgorithmProvider) open_ = library_.function<decltype(open_)>("BCryptOpenAlgorithmProvider");
    decltype(&BCryptCloseAlgorithmProvider) close_ = library_.function<decltype(close_)>("BCryptCloseAlgorithmProvider");
    decltype(&BCryptCreateHash) create_ = library_.function<decltype(create_)>("BCryptCreateHash");
    decltype(&BCryptHashData) update_ = library_.function<decltype(update_)>("BCryptHashData");
    decltype(&BCryptFinishHash) finish_ = library_.function<decltype(finish_)>("BCryptFinishHash");
    decltype(&BCryptDestroyHash) destroy_ = library_.function<decltype(destroy_)>("BCryptDestroyHash");
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
public:
    Hasher() { if (open_(&algorithm_, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0) < 0) fail(L"无法初始化 SHA-256", ERROR_NOT_SUPPORTED); }
    ~Hasher() { close_(algorithm_, 0); }
    Digest hash(const unsigned char *data, std::size_t size) const {
        BCRYPT_HASH_HANDLE state = nullptr;
        if (create_(algorithm_, &state, nullptr, 0, nullptr, 0, 0) < 0) fail(L"创建哈希失败", ERROR_NOT_ENOUGH_MEMORY);
        Digest result{};
        const auto status = update_(state, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
        const auto finished = status >= 0 ? finish_(state, result.data(), static_cast<ULONG>(result.size()), 0) : status;
        destroy_(state);
        if (finished < 0) fail(L"计算哈希失败", ERROR_INVALID_DATA);
        return result;
    }
    Digest file(HANDLE file) const {
        LARGE_INTEGER start{};
        if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) fail(L"定位缓存文件失败");
        BCRYPT_HASH_HANDLE state = nullptr;
        if (create_(algorithm_, &state, nullptr, 0, nullptr, 0, 0) < 0) fail(L"创建文件哈希失败", ERROR_NOT_ENOUGH_MEMORY);
        std::array<unsigned char, 65536> buffer{};
        DWORD read = 0;
        DWORD error = ERROR_SUCCESS;
        while (true) {
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { error = GetLastError(); break; }
            if (!read) break;
            if (update_(state, buffer.data(), read, 0) < 0) { error = ERROR_INVALID_DATA; break; }
        }
        Digest result{};
        if (!error && finish_(state, result.data(), static_cast<ULONG>(result.size()), 0) < 0) error = ERROR_INVALID_DATA;
        destroy_(state);
        if (error) fail(L"读取缓存文件失败", error);
        return result;
    }
};
struct OrdinalLess {
    bool operator()(const std::wstring &left, const std::wstring &right) const {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};
bool equal(const std::wstring &left, const std::wstring &right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
void validatePart(const std::wstring &part) {
    require(!part.empty() && part.size() <= 240 && part.back() != L'.' && part.back() != L' ', L"不安全的路径组件");
    for (const wchar_t value : part) require(value >= 32 && std::wstring(L"<>:\"/\\|?*").find(value) == std::wstring::npos, L"路径包含不安全字符");
    auto stem = part.substr(0, part.find(L'.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towupper(value)); });
    require(stem != L"CON" && stem != L"PRN" && stem != L"AUX" && stem != L"NUL" && stem != L"CONIN$" && stem != L"CONOUT$", L"设备名不可用作路径");
    if (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT"))
        require(std::wstring(L"123456789\u00b9\u00b2\u00b3").find(stem[3]) == std::wstring::npos, L"设备名不可用作路径");
}
std::wstring absolutePath(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    require(path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\', L"运行目录必须是本地盘符绝对路径（不支持 UNC/设备路径）");
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    std::size_t start = 3;
    while (start < path.size()) {
        const auto end = path.find(L'\\', start);
        validatePart(path.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    require(path.size() < 30000, L"运行目录过长");
    return L"\\\\?\\" + path;
}
std::wstring ordinary(const std::wstring &path) { return path.substr(4); }
BY_HANDLE_FILE_INFORMATION information(HANDLE file, bool directory) {
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(file, &result)) fail(L"读取路径属性失败");
    require(!(result.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), L"拒绝 reparse point / junction / symbolic link");
    require(bool(result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory, L"文件/目录类型不匹配");
    if (!directory) require(result.nNumberOfLinks == 1, L"拒绝硬链接文件");
    return result;
}
class DirectoryPins {
    std::vector<Handle> handles_;
    std::set<std::wstring, OrdinalLess> paths_;
public:
    void pin(const std::wstring &path, bool create) {
        if (paths_.contains(path)) return;
        if (create && !CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) fail(L"无法创建运行目录：" + ordinary(path));
        Handle handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle.valid()) fail(L"无法固定运行目录（可能被占用或是链接）：" + ordinary(path));
        information(handle.get(), true);
        paths_.insert(path);
        handles_.push_back(std::move(handle));
    }
    void chain(const std::wstring &path, bool create) {
        pin(path.substr(0, 7), false);
        std::size_t end = 7;
        while (end < path.size()) {
            end = path.find(L'\\', end + 1);
            pin(path.substr(0, end), create);
            if (end == std::wstring::npos) break;
        }
    }
    void chainOutside(const std::wstring &path, const BY_HANDLE_FILE_INFORMATION &forbidden) {
        const auto check = [&] {
            const auto info = information(handles_.back().get(), true);
            require(info.dwVolumeSerialNumber != forbidden.dwVolumeSerialNumber ||
                    info.nFileIndexHigh != forbidden.nFileIndexHigh ||
                    info.nFileIndexLow != forbidden.nFileIndexLow, L"日志目录不能位于运行目录内");
        };
        pin(path.substr(0, 7), false); check();
        std::size_t end = 7;
        while (end < path.size()) {
            end = path.find(L'\\', end + 1);
            pin(path.substr(0, end), true); check();
            if (end == std::wstring::npos) break;
        }
    }
};
struct Entry { std::wstring name; std::uint64_t size; std::uint64_t stored; std::uint32_t algorithm; Digest digest; const unsigned char *data; };
class Reader {
    const unsigned char *position_;
    const unsigned char *end_;
public:
    Reader(const unsigned char *start, std::size_t size) : position_(start), end_(start + size) {}
    const unsigned char *take(std::size_t size) {
        require(size <= static_cast<std::size_t>(end_ - position_), L"内嵌清单越界");
        const auto result = position_; position_ += size; return result;
    }
    std::uint64_t number(std::size_t bytes) {
        const auto data = take(bytes);
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < bytes; ++index) result |= std::uint64_t(data[index]) << (index * 8);
        return result;
    }
    bool done() const { return position_ == end_; }
};
std::vector<Entry> manifest(const unsigned char *payload, std::size_t bytes) {
    Reader header(payload, bytes);
    require(std::memcmp(header.take(8), "RSPK0001", 8) == 0, L"未知的内嵌包格式");
    const auto count = header.number(4);
    const auto tableSize = header.number(4);
    require(count > 0 && count <= 1024 && tableSize <= 1024 * 1024, L"内嵌清单超限");
    Reader table(header.take(static_cast<std::size_t>(tableSize)), static_cast<std::size_t>(tableSize));
    std::vector<Entry> result;
    std::set<std::wstring, OrdinalLess> names;
    std::uint64_t total = 0;
    bool app = false;
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto length = table.number(4);
        Entry entry{};
        entry.algorithm = static_cast<std::uint32_t>(table.number(4));
        entry.size = table.number(8); entry.stored = table.number(8);
        std::memcpy(entry.digest.data(), table.take(32), 32);
        require(length > 0 && length <= 4096 && entry.size <= kMaxFile && entry.stored <= kMaxFile && (entry.algorithm == 0 || entry.algorithm == COMPRESS_ALGORITHM_XPRESS_HUFF), L"内嵌文件记录超限");
        const auto name = table.take(static_cast<std::size_t>(length));
        entry.name = utf16(std::string(reinterpret_cast<const char *>(name), static_cast<std::size_t>(length)));
        require(entry.name.front() != L'/' && entry.name.back() != L'/' && entry.name.find(L'\\') == std::wstring::npos, L"不安全的清单路径");
        std::size_t start = 0;
        while (start < entry.name.size()) {
            const auto end = entry.name.find(L'/', start);
            const auto part = entry.name.substr(start, end == std::wstring::npos ? end : end - start);
            validatePart(part);
            require(!equal(part, kStage) && !equal(part, kLock) && !equal(part, kMarker), L"清单与内部文件名冲突");
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        std::replace(entry.name.begin(), entry.name.end(), L'/', L'\\');
        require(names.insert(entry.name).second, L"清单路径重复");
        if (equal(entry.name, PORTABLE_MAIN_EXECUTABLE)) app = true;
        total += entry.size;
        require(total <= kMaxTotal && (entry.algorithm != 0 || entry.stored == entry.size), L"内嵌文件总大小超限");
        entry.data = header.take(static_cast<std::size_t>(entry.stored));
        result.push_back(std::move(entry));
    }
    require(table.done() && header.done() && app, L"内嵌清单结构错误");
    for (const auto &entry : result) {
        auto parent = entry.name.find(L'\\');
        while (parent != std::wstring::npos) {
            require(!names.contains(entry.name.substr(0, parent)), L"清单文件与目录冲突");
            parent = entry.name.find(L'\\', parent + 1);
        }
    }
    return result;
}
Handle openFile(const std::wstring &path, DWORD access = GENERIC_READ, DWORD disposition = OPEN_EXISTING, DWORD sharing = FILE_SHARE_READ) {
    Handle file(CreateFileW(path.c_str(), access, sharing, nullptr, disposition, kFileFlags, nullptr));
    if (!file.valid()) {
        if (disposition == OPEN_EXISTING && GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        fail(L"无法打开运行文件：" + ordinary(path));
    }
    information(file.get(), false);
    return file;
}
void removeOwned(const std::wstring &path) {
    auto file = openFile(path, DELETE | FILE_READ_ATTRIBUTES);
    if (!file.valid()) return;
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(file.get(), FileDispositionInfo, &disposition, sizeof(disposition))) fail(L"无法替换损坏的运行文件：" + ordinary(path));
}
class ExtractionLock {
    Handle file_;
public:
    explicit ExtractionLock(const std::wstring &root) {
        const auto deadline = GetTickCount64() + 120000;
        while (true) {
            file_ = Handle(CreateFileW((root + L"\\" + kLock).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, kFileFlags, nullptr));
            if (file_.valid()) { information(file_.get(), false); break; }
            const auto error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION || GetTickCount64() >= deadline) fail(L"无法取得运行包提取锁", error);
            Sleep(50);
        }
    }
};
class Decompressor {
    Library library_{L"cabinet.dll"};
    decltype(&CreateDecompressor) create_ = library_.function<decltype(create_)>("CreateDecompressor");
    decltype(&Decompress) decompress_ = library_.function<decltype(decompress_)>("Decompress");
    decltype(&CloseDecompressor) close_ = library_.function<decltype(close_)>("CloseDecompressor");
    DECOMPRESSOR_HANDLE handle_ = nullptr;
public:
    Decompressor() { if (!create_(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &handle_)) fail(L"无法初始化系统解压组件"); }
    ~Decompressor() { close_(handle_); }
    Bytes unpack(const Entry &entry) {
        Bytes result(static_cast<std::size_t>(entry.size));
        if (entry.algorithm == 0) { if (!result.empty()) std::memcpy(result.data(), entry.data, result.size()); }
        else {
            SIZE_T actual = 0;
            if (!decompress_(handle_, entry.data, static_cast<SIZE_T>(entry.stored), result.data(), result.size(), &actual)) fail(L"内嵌文件解压失败");
            require(actual == result.size(), L"解压文件大小不匹配");
        }
        return result;
    }
};
void publish(const std::wstring &target, const unsigned char *data, std::size_t size) {
    auto file = openFile(target, GENERIC_WRITE | FILE_READ_ATTRIBUTES, CREATE_NEW, 0);
    std::size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(65536, size - offset));
        if (!WriteFile(file.get(), data + offset, chunk, &written, nullptr) || !written) fail(L"写入运行文件失败");
        offset += written;
    }
    if (!FlushFileBuffers(file.get())) fail(L"提交运行文件失败");
}
using Layout = std::map<std::wstring, std::set<std::wstring, OrdinalLess>, OrdinalLess>;
Layout layout(const std::vector<Entry> &entries, bool internal) {
    Layout result;
    result[L""];
    for (const auto &entry : entries) {
        std::wstring parent;
        std::size_t start = 0;
        while (true) {
            const auto end = entry.name.find(L'\\', start);
            const auto part = entry.name.substr(start, end == std::wstring::npos ? end : end - start);
            result[parent].insert(part);
            if (end == std::wstring::npos) break;
            parent = entry.name.substr(0, end); result[parent]; start = end + 1;
        }
    }
    if (internal) {
        result[L""].insert(kLock); result[L""].insert(kMarker);
        result[L""].insert(kStage);
    }
    return result;
}
void checkLayout(const std::wstring &root, const Layout &expected) {
    for (const auto &[relative, allowed] : expected) {
        const auto directory = root + (relative.empty() ? L"" : L"\\" + relative);
        WIN32_FIND_DATAW item{};
        const auto search = FindFirstFileW((directory + L"\\*").c_str(), &item);
        if (search == INVALID_HANDLE_VALUE) fail(L"无法检查运行目录内容");
        DWORD error = ERROR_SUCCESS;
        do {
            const std::wstring name(item.cFileName);
            if (name == L"." || name == L"..") continue;
            if ((item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !allowed.contains(name)) { error = ERROR_INVALID_DATA; break; }
        } while (FindNextFileW(search, &item));
        if (!error && GetLastError() != ERROR_NO_MORE_FILES) error = GetLastError();
        FindClose(search);
        if (error) fail(L"运行目录含链接或未知文件；请人工检查，不会删除未知内容", error);
    }
}
bool matches(HANDLE file, std::uint64_t size, const Digest &digest, const Hasher &hasher) {
    const auto info = information(file, false);
    return ((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) == size && hasher.file(file) == digest;
}
bool isDll(const std::wstring &name) { return name.size() >= 4 && equal(name.substr(name.size() - 4), L".dll"); }
std::wstring argumentTail() {
    const wchar_t *cursor = GetCommandLineW();
    bool quoted = false;
    while (*cursor) {
        if (*cursor == L'"') quoted = !quoted;
        else if (!quoted && (*cursor == L' ' || *cursor == L'\t')) break;
        ++cursor;
    }
    return cursor;
}
// This is deliberately conservative: an ambiguous or unsupported override
// disables the optional record instead of selecting a different data folder.
std::wstring exitLogDirectory(const std::wstring &runtimeRoot) {
    Library shell(L"shell32.dll");
    using ParseCommandLine = LPWSTR *(WINAPI *)(LPCWSTR, int *);
    const auto parse = shell.function<ParseCommandLine>("CommandLineToArgvW");
    const auto command = std::wstring(L"\"") + PORTABLE_MAIN_EXECUTABLE + L"\"" + argumentTail();
    int count = 0;
    struct Arguments {
        LPWSTR *value;
        ~Arguments() { if (value) LocalFree(value); }
    } args{parse(command.c_str(), &count)};
    if (!args.value || count < 1) return {};
    bool hasOverride = false;
    std::wstring base;
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = args.value[i];
        if (arg == L"--") break;
        if (arg == L"--data-dir") {
            hasOverride = true;
            if (++i >= count) return {};
            base = args.value[i];
        } else if (arg.starts_with(L"--data-dir=")) {
            hasOverride = true;
            base = arg.substr(11);
        } else if (arg.starts_with(L"--data-dir") || arg.starts_with(L"-data-dir")) {
            return {};
        }
    }
    if (!hasOverride) {
        base = environment(L"RABBITSHELL_DATA_DIR");
        if (base.empty()) {
            base = environment(L"LOCALAPPDATA");
            if (base.empty()) return {};
            base += L"\\RabbitShell\\RabbitShell";
        }
    }
    if (base.empty()) return {};
    // absolutePath rejects relative, UNC, device and ambiguous components.
    const auto data = absolutePath(base);
    const auto logs = data + (data.back() == L'\\' ? L"logs" : L"\\logs");
    if (logs.size() >= runtimeRoot.size() &&
        CompareStringOrdinal(logs.c_str(), static_cast<int>(runtimeRoot.size()),
                             runtimeRoot.c_str(), static_cast<int>(runtimeRoot.size()), TRUE) == CSTR_EQUAL &&
        (runtimeRoot.back() == L'\\' || logs.size() == runtimeRoot.size() || logs[runtimeRoot.size()] == L'\\')) return {};
    return logs;
}
[[maybe_unused]] void writeChildExit(DWORD childPid, DWORD code) {
    const auto overridePath = environment(L"RABBITSHELL_PORTABLE_RUNTIME");
    const auto local = environment(L"LOCALAPPDATA");
    if (overridePath.empty() && local.empty()) return;
    const auto runtimeRoot = absolutePath(overridePath.empty() ? local + L"\\RabbitShell\\runtime\\" + utf16(kPayloadHash) : overridePath);
    const auto logs = exitLogDirectory(runtimeRoot);
    if (logs.empty()) return;
    // The pinned, non-reparse directory chain also prevents an apparent data
    // path, including an 8.3 alias, from redirecting into the runtime.
    Handle runtime(CreateFileW(runtimeRoot.c_str(), FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!runtime.valid()) return;
    const auto runtimeInfo = information(runtime.get(), true);
    DirectoryPins directories;
    directories.chainOutside(logs, runtimeInfo);
    struct Slot { std::wstring path; FILETIME modified{}; bool empty = true; };
    std::vector<Slot> candidates;
    for (int i = 0; i < 16; ++i) {
        Slot slot;
        slot.path = logs + L"\\exit-" + (i < 10 ? L"0" : L"") + std::to_wstring(i) + L".log";
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(slot.path.c_str(), GetFileExInfoStandard, &attributes)) {
            slot.modified = attributes.ftLastWriteTime;
            slot.empty = attributes.nFileSizeHigh == 0 && attributes.nFileSizeLow == 0;
        }
        candidates.push_back(std::move(slot));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Slot &a, const Slot &b) {
        if (a.empty != b.empty) return a.empty;
        return CompareFileTime(&a.modified, &b.modified) < 0;
    });
    SYSTEMTIME utc{}; GetSystemTime(&utc);
    const auto padded = [](unsigned value, std::size_t width) {
        auto text = std::to_string(value);
        if (text.size() < width) text.insert(0, width - text.size(), '0');
        return text;
    };
    const std::string row = std::string("{\"event\":\"ChildExit\",\"version\":\"") + RABBITSHELL_PORTABLE_VERSION +
        "\",\"utc\":\"" + padded(utc.wYear, 4) + "-" + padded(utc.wMonth, 2) + "-" + padded(utc.wDay, 2) +
        "T" + padded(utc.wHour, 2) + ":" + padded(utc.wMinute, 2) + ":" + padded(utc.wSecond, 2) +
        "." + padded(utc.wMilliseconds, 3) + "Z\",\"launcherPid\":" + std::to_string(GetCurrentProcessId()) +
        ",\"childPid\":" + std::to_string(childPid) + ",\"exitCode\":" + std::to_string(code) + "}\n";
    for (const auto &slot : candidates) {
        try {
            Handle file(CreateFileW(slot.path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                    nullptr, OPEN_ALWAYS, kFileFlags, nullptr));
            if (!file.valid()) continue;
            information(file.get(), false); // Reject reparse points and hard links before truncating.
            LARGE_INTEGER start{};
            if (!SetFilePointerEx(file.get(), start, nullptr, FILE_BEGIN) || !SetEndOfFile(file.get())) continue;
            DWORD written = 0;
            if (!WriteFile(file.get(), row.data(), static_cast<DWORD>(row.size()), &written, nullptr) || written != row.size()) {
                if (SetFilePointerEx(file.get(), start, nullptr, FILE_BEGIN)) SetEndOfFile(file.get());
            }
            return;
        } catch (...) { /* Try another slot; logging must not change the exit code. */ }
    }
}
std::vector<std::wstring> portableOrigin(const Hasher &hasher) {
    std::array<wchar_t,32768> image{};
    const auto length=GetModuleFileNameW(nullptr,image.data(),static_cast<DWORD>(image.size()));
    require(length>0&&length<image.size(),L"无法确定启动器路径");
    auto file=openFile(image.data());require(file.valid(),L"无法读取启动器身份");
    const auto before=information(file.get(),false);
    std::array<wchar_t,32768> finalPath{};
    const auto finalLength=GetFinalPathNameByHandleW(file.get(),finalPath.data(),static_cast<DWORD>(finalPath.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    require(finalLength>0&&finalLength<finalPath.size(),L"无法规范化启动器路径");
    const auto canonical=ordinary(std::wstring(finalPath.data(),finalLength));
    // The installer supports only canonical local DOS paths. An otherwise
    // runnable wrapper on an unsupported path remains a manual-update client.
    require(canonical.size()>3&&canonical[1]==L':'&&canonical[2]==L'\\',L"此路径需手动更新");
    const auto digest=hasher.file(file.get());const auto after=information(file.get(),false);
    require(before.dwVolumeSerialNumber==after.dwVolumeSerialNumber&&before.nFileIndexHigh==after.nFileIndexHigh&&before.nFileIndexLow==after.nFileIndexLow&&before.nFileSizeHigh==after.nFileSizeHigh&&before.nFileSizeLow==after.nFileSizeLow&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0,L"启动器文件身份已变化");
    FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))fail(L"无法读取启动器创建时间");
    const auto number=[](DWORD high,DWORD low){return std::to_wstring((std::uint64_t(high)<<32)|low);};
    const std::wstring prefix=L"RABBITSHELL_PORTABLE_ORIGIN_";
    return {prefix+L"KIND=single-exe-v1",prefix+L"PATH="+canonical,prefix+L"PID="+std::to_wstring(GetCurrentProcessId()),
        prefix+L"CREATED="+number(created.dwHighDateTime,created.dwLowDateTime),prefix+L"VOLUME="+std::to_wstring(before.dwVolumeSerialNumber),
        prefix+L"INDEX="+number(before.nFileIndexHigh,before.nFileIndexLow),prefix+L"BYTES="+number(before.nFileSizeHigh,before.nFileSizeLow),
        prefix+L"MODIFIED="+number(before.ftLastWriteTime.dwHighDateTime,before.ftLastWriteTime.dwLowDateTime),prefix+L"SHA256="+utf16(hex(digest))};
}
std::vector<wchar_t> childEnvironment(const std::wstring &root,bool developer,const Hasher &hasher) {
    const auto block = GetEnvironmentStringsW();
    if (!block) fail(L"读取子进程环境失败");
    std::vector<std::wstring> variables;
    for (auto cursor = block; *cursor; cursor += std::wcslen(cursor) + 1) variables.emplace_back(cursor);
    FreeEnvironmentStringsW(block);
    std::array<wchar_t, 32768> system{};
    const auto length = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    require(length > 0 && length < system.size(), L"无法确定系统目录");
    std::vector<std::wstring> selected;
    for (const auto &item : variables) {
        const auto split = item.find(L'=', item.front() == L'=' ? 1 : 0);
        const auto key = item.substr(0, split);
        const auto value = split == std::wstring::npos ? L"" : item.substr(split + 1);
        auto upper = key;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });
        if (equal(key, L"PATH") || upper.starts_with(L"RABBITSHELL_PORTABLE_") || upper.starts_with(L"QML") || upper.starts_with(L"QT")) {
            if (!equal(key, L"QT_QPA_PLATFORM") || (value != L"offscreen" && value != L"windows")) continue;
        }
        selected.push_back(item);
    }
    const auto path = L"PATH=" + ordinary(root) + L";" + std::wstring(system.data(), length);
    selected.push_back(path);
    if(!developer){
        try{const auto origin=portableOrigin(hasher);selected.insert(selected.end(),origin.begin(),origin.end());}
        catch(const Failure &){/* Missing trustworthy origin means manual update, never a fabricated identity. */}
    }
    std::sort(selected.begin(), selected.end(), OrdinalLess{});
    std::vector<wchar_t> result;
    for (const auto &item : selected) {
        result.insert(result.end(), item.begin(), item.end()); result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}
class StandardHandles {
    std::array<Handle, 3> handles_;
    std::array<HANDLE, 3> raw_{};
    Bytes attributes_;
public:
    STARTUPINFOEXW startup{};
    StandardHandles() {
        constexpr std::array<DWORD, 3> identifiers{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (std::size_t index = 0; index < identifiers.size(); ++index) {
            HANDLE duplicate = nullptr;
            const auto source = GetStdHandle(identifiers[index]);
            if (source && source != INVALID_HANDLE_VALUE && DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS)) handles_[index] = Handle(duplicate);
            else {
                SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
                handles_[index] = Handle(CreateFileW(L"NUL", index == 0 ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
                if (!handles_[index].valid()) fail(L"无法创建子进程标准句柄");
            }
            raw_[index] = handles_[index].get();
        }
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        require(bytes > 0 && bytes <= 65536, L"无法确定子进程属性大小");
        attributes_.resize(bytes);
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = raw_[0]; startup.StartupInfo.hStdOutput = raw_[1]; startup.StartupInfo.hStdError = raw_[2];
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) fail(L"初始化子进程属性失败");
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, raw_.data(), sizeof(raw_), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            fail(L"设置子进程句柄白名单失败");
        }
    }
    ~StandardHandles() { DeleteProcThreadAttributeList(startup.lpAttributeList); }
};
struct Child { Handle process; Handle thread; DirectoryPins directories; std::vector<Handle> files; };
Child launch() {
    Hasher hasher;
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) fail(L"找不到内嵌运行包");
    const auto bytes = SizeofResource(nullptr, resource);
    const auto data = static_cast<const unsigned char *>(LockResource(LoadResource(nullptr, resource)));
    require(data && bytes == kPayloadSize && hex(hasher.hash(data, bytes)) == kPayloadHash, L"内嵌运行包完整性校验失败");
    auto entries = manifest(data, bytes);
    const auto overridePath = environment(L"RABBITSHELL_PORTABLE_RUNTIME");
    const bool developer = !overridePath.empty();
    const auto local = environment(L"LOCALAPPDATA");
    require(developer || !local.empty(), L"未设置 LOCALAPPDATA");
    const auto root = absolutePath(developer ? overridePath : local + L"\\RabbitShell\\runtime\\" + utf16(kPayloadHash));
    DirectoryPins directories;
    directories.chain(root, !developer);
    const auto expected = layout(entries, !developer);
    for (const auto &[relative, names] : expected) {
        (void)names;
        if (!relative.empty()) directories.pin(root + L"\\" + relative, !developer);
    }
    std::vector<Handle> verified;
    const auto prepare = [&] {
        checkLayout(root, expected);
        if (!developer) removeOwned(root + L"\\" + kStage);
        std::vector<std::size_t> damaged;
        verified.reserve(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto &entry = entries[index];
            auto file = openFile(root + L"\\" + entry.name);
            bool valid = file.valid();
            if (valid && developer && isDll(entry.name)) {
                const auto info = information(file.get(), false);
                const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
                require(size > 0 && size <= kMaxFile, L"开发者 DLL 大小超限");
            } else if (valid) valid = matches(file.get(), entry.size, entry.digest, hasher);
            if (!valid) {
                require(!developer, L"开发者运行目录缺文件或非 DLL 文件与原包不一致");
                damaged.push_back(index);
            } else verified.push_back(std::move(file));
        }
        if (!developer) {
            const std::string marker = std::string(kPayloadHash) + "\n";
            bool markerValid = false;
            {
                auto file = openFile(root + L"\\" + kMarker);
                markerValid = file.valid() && matches(file.get(), marker.size(), hasher.hash(reinterpret_cast<const unsigned char *>(marker.data()), marker.size()), hasher);
            }
            if (!damaged.empty() || !markerValid) {
                removeOwned(root + L"\\" + kMarker);
                if (!damaged.empty()) {
                    Decompressor unpacker;
                    for (const auto index : damaged) {
                        const auto &entry = entries[index];
                        auto content = unpacker.unpack(entry);
                        require(hasher.hash(content.data(), content.size()) == entry.digest, L"解压文件完整性校验失败");
                        const auto target = root + L"\\" + entry.name;
                        removeOwned(target);
                        publish(target, content.data(), content.size());
                        auto file = openFile(target);
                        require(file.valid() && matches(file.get(), entry.size, entry.digest, hasher), L"写入后的文件校验失败");
                        verified.push_back(std::move(file));
                    }
                }
                publish(root + L"\\" + kMarker, reinterpret_cast<const unsigned char *>(marker.data()), marker.size());
                auto file = openFile(root + L"\\" + kMarker);
                require(file.valid() && matches(file.get(), marker.size(), hasher.hash(reinterpret_cast<const unsigned char *>(marker.data()), marker.size()), hasher), L"完成标记写入后的校验失败");
            }
        }
        checkLayout(root, expected);
        const auto application = root + L"\\" + PORTABLE_MAIN_EXECUTABLE;
        const auto command = L"\"" + ordinary(application) + L"\"" + argumentTail();
        require(command.size() < 32767, L"命令行过长");
        auto environmentBlock = childEnvironment(root,developer,hasher);
        auto mutableCommand = command;
        StandardHandles standard;
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
        if (environment(L"RABBITSHELL_PORTABLE_NO_DIALOG") == L"1") {
            standard.startup.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
            standard.startup.StartupInfo.wShowWindow = SW_HIDE;
            flags |= CREATE_NO_WINDOW;
        }
        PROCESS_INFORMATION process{};
        if (!SetDllDirectoryW(ordinary(root).c_str())) fail(L"无法设置安全 DLL 目录");
        if (!CreateProcessW(application.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, flags, environmentBlock.data(), ordinary(root).c_str(), &standard.startup.StartupInfo, &process)) fail(std::wstring(L"无法启动 ")+PORTABLE_DISPLAY_NAME);
        Child child{Handle(process.hProcess), Handle(process.hThread), {}, {}};
        if (ResumeThread(child.thread.get()) == static_cast<DWORD>(-1)) {
            TerminateProcess(child.process.get(), 126);
            fail(std::wstring(L"无法运行 ")+PORTABLE_DISPLAY_NAME+L" 子进程");
        }
        return child;
    };
    Child child;
    {
        if (!developer) { ExtractionLock lock(root); child = prepare(); }
        else child = prepare();
    }
    child.directories = std::move(directories);
    child.files = std::move(verified);
    return child;
}
void report(const std::wstring &message, DWORD code) {
    const auto text = std::wstring(PORTABLE_DISPLAY_NAME) + L" 单文件启动失败：" + message + L"\nWindows/error code: " + std::to_wstring(code) + L"\n";
    const auto output = GetStdHandle(STD_ERROR_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string encoded(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), encoded.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(output, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr);
    }
    if (environment(L"RABBITSHELL_PORTABLE_NO_DIALOG") != L"1") {
        Library user32(L"user32.dll");
        user32.function<decltype(&MessageBoxW)>("MessageBoxW")(nullptr, text.c_str(), PORTABLE_DISPLAY_NAME, MB_OK | MB_ICONERROR);
    }
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32) || !SetDllDirectoryW(L"")) fail(L"无法启用安全 DLL 搜索");
        auto child = launch();
        child.thread = Handle();
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
        if (WaitForSingleObject(child.process.get(), INFINITE) != WAIT_OBJECT_0) fail(L"等待子进程失败");
        DWORD code = 0;
        if (!GetExitCodeProcess(child.process.get(), &code)) fail(L"读取子进程退出码失败");
        // The Qt child owns diagnostics settings, including the default-off
        // policy and custom directory. Propagate its exit code without logging.
        return static_cast<int>(code);
    } catch (const Failure &error) {
        try { report(error.message, error.code); } catch (...) {}
    } catch (...) {
        try { report(L"启动器资源不足或发生内部错误", ERROR_NOT_ENOUGH_MEMORY); } catch (...) {}
    }
    return 125;
}
