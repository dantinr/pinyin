#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include <iostream>

namespace {
using namespace pinyin::testing;
struct HostRuntime {
    std::vector<HMODULE> libraries;
    explicit HostRuntime(const std::filesystem::path& directory) {
        // This executable uses /MT. Preload the host's runtime before loading
        // the /MD IME, just as an application with its own VC runtime does.
        try {
            for (const auto name : {L"VCRUNTIME140.dll", L"VCRUNTIME140_1.dll", L"MSVCP140.dll"}) {
                const auto path = directory / name;
                const auto library = LoadLibraryExW(path.c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (!library) throw std::runtime_error("cannot preload host runtime");
                libraries.push_back(library);
            }
        } catch (...) { release(); throw; }
    }
    void release() noexcept { for (auto i = libraries.rbegin(); i != libraries.rend(); ++i) FreeLibrary(*i); libraries.clear(); }
    ~HostRuntime() { release(); }
};
struct DocumentWindow {
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Private Pinyin host runtime test",
        WS_POPUP | WS_VISIBLE, 24, 24, 640, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    DocumentWindow() { if (!window) throw std::runtime_error("cannot create test document window"); }
    ~DocumentWindow() { if (window) DestroyWindow(window); }
};
}

int wmain(int argc, wchar_t* argv[]) {
    int checks = 0, result = 0;
    auto check = [&](bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); };
    HRESULT initialized = E_FAIL;
    try {
        check(argc == 2 || argc == 3, "expected IME DLL and optional host runtime directory");
        wchar_t system[32768]{};
        check(GetSystemDirectoryW(system, static_cast<UINT>(std::size(system))) != 0, "cannot find system runtime directory");
        const auto directory = std::filesystem::absolute(argc == 3 ? argv[2] : system);
        HostRuntime runtime(directory);
        std::wcout << L"Preloaded host runtime from: " << directory.c_str() << std::endl;
        initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        check(SUCCEEDED(initialized), "cannot initialize COM");
        TestWorkspace workspace;
        DocumentWindow owner;
        Harness ime(argv[1]); ime.store->window = owner.window;
        for (unsigned i = 0; i < 10; ++i) {
            const auto before = ime.store->text;
            ime.type("nihao");
            check(ime.compositions() == 1, "host runtime prevented composition start");
            ime.key(VK_SPACE);
            check(ime.store->text == before + L"你好" && ime.compositions() == 0, "host runtime prevented candidate commit");
            ime.type("nihao"); ime.key(VK_ESCAPE);
            check(ime.store->text == before + L"你好" && ime.compositions() == 0, "host runtime prevented composition cancellation");
        }
        ime.store->window = nullptr;
        DestroyWindow(owner.window); owner.window = nullptr;
        Harness::pump();
        ime.close(); check(ime.unload_result == S_OK, "host runtime test leaked DLL references");
        std::wcout << checks << L" host runtime TSF checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
