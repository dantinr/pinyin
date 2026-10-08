#include <initguid.h>
#include "common.hpp"

namespace pinyin::ime {
HINSTANCE module = nullptr;
std::atomic<long> objects{0};
std::atomic<long> server_locks{0};
namespace {
class Factory final : public IClassFactory, private ModuleObject {
    std::atomic<ULONG> refs_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return create_service(iid, result);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++server_locks;
        else {
            auto count = server_locks.load();
            do { if (count == 0) return E_UNEXPECTED; }
            while (!server_locks.compare_exchange_weak(count, count - 1));
        }
        return S_OK;
    }
};
std::wstring class_key() {
    wchar_t guid[40]{}; StringFromGUID2(service_id, guid, 40);
    return std::wstring(L"Software\\Classes\\CLSID\\") + guid;
}
HRESULT registry_value(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY handle = nullptr;
    auto error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &handle, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle); return HRESULT_FROM_WIN32(error);
}
constexpr const GUID* categories[] = {
    &GUID_TFCAT_TIP_KEYBOARD, &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, &GUID_TFCAT_TIPCAP_UIELEMENTENABLED
};
HRESULT unregister_server() {
    HRESULT result = S_OK;
    ComPtr<ITfCategoryMgr> category;
    auto hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&category));
    if (SUCCEEDED(hr)) {
        for (const auto guid : categories) {
            hr = category->UnregisterCategory(service_id, *guid, service_id);
            if (FAILED(hr) && SUCCEEDED(result)) result = hr;
        }
    } else result = hr;
    ComPtr<ITfInputProcessorProfiles> profiles;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) {
        hr = profiles->Unregister(service_id);
        if (FAILED(hr) && SUCCEEDED(result)) result = hr;
    } else if (SUCCEEDED(result)) result = hr;
    const auto error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, class_key().c_str());
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND && SUCCEEDED(result)) result = HRESULT_FROM_WIN32(error);
    return result;
}
HRESULT register_server() {
    const auto path = module_path().wstring();
    auto hr = registry_value(class_key(), nullptr, description);
    if (FAILED(hr)) return hr;
    hr = registry_value(class_key() + L"\\InprocServer32", nullptr, path);
    if (SUCCEEDED(hr)) hr = registry_value(class_key() + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    ComPtr<ITfInputProcessorProfiles> profiles;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) hr = profiles->Register(service_id);
    if (SUCCEEDED(hr)) hr = profiles->AddLanguageProfile(service_id, language_id, profile_id,
        description, static_cast<ULONG>(std::size(description) - 1), path.data(), static_cast<ULONG>(path.size()), 0);
    if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfile(service_id, language_id, profile_id, TRUE);
    ComPtr<ITfCategoryMgr> category;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&category));
    if (SUCCEEDED(hr)) {
        for (const auto guid : categories) {
            hr = category->RegisterCategory(service_id, *guid, service_id);
            if (FAILED(hr)) break;
        }
    }
    if (FAILED(hr)) unregister_server();
    return hr;
}
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { pinyin::ime::module = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return pinyin::ime::objects == 0 && pinyin::ime::server_locks == 0 ? S_OK : S_FALSE;
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    using namespace pinyin::ime;
    if (!result) return E_POINTER; *result = nullptr;
    if (clsid != service_id) return CLASS_E_CLASSNOTAVAILABLE;
    return protect([&] { auto factory = new Factory; auto hr = factory->QueryInterface(iid, result); factory->Release(); return hr; });
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    using namespace pinyin::ime;
    return protect([] { Apartment apartment; return apartment.usable() ? register_server() : apartment.result; });
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    using namespace pinyin::ime;
    return protect([] { Apartment apartment; return apartment.usable() ? unregister_server() : apartment.result; });
}
