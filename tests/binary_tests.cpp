#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

bool systemImports(HMODULE handle, const char *label) {
    const auto base =
        reinterpret_cast<const BYTE *>(reinterpret_cast<uintptr_t>(handle) & ~uintptr_t(3));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return true;
    auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + rva);
    for (; imports->Name; ++imports) {
        std::string name(reinterpret_cast<const char *>(base + imports->Name));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(tolower(c)); });
        bool system = name == "kernel32.dll" || name == "user32.dll" || name == "gdi32.dll" ||
                      name == "shell32.dll" || name == "dinput8.dll" || name == "ntdll.dll" ||
                      name == "ws2_32.dll" || name == "msvcrt.dll" || name == "ucrtbase.dll" ||
                      name == "advapi32.dll" || name.rfind("api-ms-win-", 0) == 0;
        printf("IMPORT %s: %s\n", label, name.c_str());
        if (!system) {
            printf("FAIL unexpected external runtime dependency: %s\n", name.c_str());
            return false;
        }
    }
    return true;
}
int main() {
    HMODULE module = LoadLibraryW(L"build\\MozaComfortPlugin.dll");
    if (!module) {
        printf("FAIL LoadLibrary: %lu\n", GetLastError());
        return 1;
    }
    for (const char *name :
         {"scs_input_init", "scs_input_shutdown", "scs_telemetry_init", "scs_telemetry_shutdown"}) {
        if (!GetProcAddress(module, name)) {
            printf("FAIL export %s\n", name);
            FreeLibrary(module);
            return 1;
        }
        printf("PASS production DLL export %s\n", name);
    }
    if (!systemImports(module, "plugin")) {
        FreeLibrary(module);
        return 1;
    }
    FreeLibrary(module);
    printf("PASS production DLL loads with Windows system dependencies only\n");
    HMODULE executable =
        LoadLibraryExW(L"build\\MozaComfort.exe", nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!executable || !systemImports(executable, "helper")) {
        if (executable)
            FreeLibrary(executable);
        return 1;
    }
    FreeLibrary(executable);
    printf("PASS portable helper imports Windows system dependencies only\n");
    return 0;
}
