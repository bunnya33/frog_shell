#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

int wmain(int argc, wchar_t **argv) {
    wchar_t output[32768]{};
    if (!GetEnvironmentVariableW(L"RABBITSHELL_PROBE_REPORT", output, 32768)) return 90;
    FILE *file = nullptr;
    if (_wfopen_s(&file, output, L"wb") || !file) return 91;
    const auto write = [file](const std::wstring &value) {
        const auto bytes = static_cast<std::uint32_t>(value.size() * sizeof(wchar_t));
        return std::fwrite(&bytes, sizeof(bytes), 1, file) == 1 && std::fwrite(value.data(), 1, bytes, file) == bytes;
    };
    wchar_t directory[32768]{};
    wchar_t path[32768]{};
    wchar_t pluginPath[32768]{};
    GetCurrentDirectoryW(32768, directory);
    GetEnvironmentVariableW(L"PATH", path, 32768);
    GetEnvironmentVariableW(L"QT_PLUGIN_PATH", pluginPath, 32768);
    bool okay = write(GetCommandLineW()) && write(directory) && write(path) && write(pluginPath);
    for (int index = 0; index < argc; ++index) okay = write(argv[index]) && okay;
    std::fclose(file);
    std::fputs("portable probe stdout\n", stdout);
    std::fputs("portable probe stderr\n", stderr);
    Sleep(1000);
    return okay ? 37 : 92;
}
