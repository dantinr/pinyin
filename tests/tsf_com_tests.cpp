#include "common.hpp"
#include <iostream>

using namespace pinyin::ime;
namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void success(HRESULT hr, const char* message) {
    ++checks;
    if (FAILED(hr)) throw std::runtime_error(std::string(message) + ": " + std::to_string(static_cast<unsigned long>(hr)));
}
}
int wmain(int argc, wchar_t* argv[]) {
    HMODULE dll = nullptr;
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(apartment), "test setup failed");
        dll = LoadLibraryW(argv[1]); check(dll != nullptr, "cannot load DLL");
        auto factory_function = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll, "DllGetClassObject"));
        auto unload = reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(dll, "DllCanUnloadNow"));
        check(factory_function && unload && GetProcAddress(dll, "DllRegisterServer") && GetProcAddress(dll, "DllUnregisterServer"), "missing COM exports");
        check(unload() == S_OK, "idle DLL is not unloadable");
        {
            ComPtr<IClassFactory> factory; success(factory_function(service_id, IID_PPV_ARGS(&factory)), "cannot obtain class factory");
            check(unload() == S_FALSE, "live factory not counted");
            void* absent = nullptr;
            check(factory_function(GUID_NULL, IID_IUnknown, &absent) == CLASS_E_CLASSNOTAVAILABLE, "unknown CLSID accepted");
            check(factory->CreateInstance(factory.Get(), IID_IUnknown, &absent) == CLASS_E_NOAGGREGATION, "aggregation accepted");
            success(factory->LockServer(TRUE), "server lock failed"); success(factory->LockServer(FALSE), "server unlock failed");
            check(factory->LockServer(FALSE) == E_UNEXPECTED, "server lock underflow");
            ComPtr<ITfTextInputProcessorEx> service;
            success(factory->CreateInstance(nullptr, IID_PPV_ARGS(&service)), "cannot create text service");
            ComPtr<ITfKeyEventSink> keys; success(service.As(&keys), "key sink interface missing");
            BOOL eaten = TRUE; success(keys->OnTestKeyDown(nullptr, 'A', 0, &eaten), "null-context key test failed");
            check(!eaten, "null-context input swallowed");
            check(keys->OnTestKeyDown(nullptr, 'A', 0, nullptr) == E_POINTER, "null output pointer accepted");
            ComPtr<ITfDisplayAttributeProvider> provider; success(service.As(&provider), "display provider missing");
            ComPtr<IEnumTfDisplayAttributeInfo> attributes; success(provider->EnumDisplayAttributeInfo(&attributes), "cannot enumerate attributes");
            ComPtr<ITfDisplayAttributeInfo> attribute; ULONG fetched = 0;
            success(attributes->Next(1, &attribute, &fetched), "cannot get display attribute"); check(fetched == 1, "wrong attribute count");
            GUID guid{}; success(attribute->GetGUID(&guid), "cannot get attribute GUID"); check(guid == attribute_id, "wrong attribute GUID");
            TF_DISPLAYATTRIBUTE appearance{}; success(attribute->GetAttributeInfo(&appearance), "cannot get attribute appearance");
            check(appearance.lsStyle == TF_LS_SOLID && appearance.bAttr == TF_ATTR_INPUT, "preedit underline missing");
            ComPtr<ITfDisplayAttributeInfo> second;
            check(attributes->Next(1, &second, &fetched) == S_FALSE && fetched == 0, "attribute enumeration did not finish");
            success(attributes->Reset(), "attribute reset failed");
            ComPtr<IEnumTfDisplayAttributeInfo> clone; success(attributes->Clone(&clone), "attribute clone failed");
            success(clone->Next(1, &second, &fetched), "attribute clone position incorrect");
            check(provider->GetDisplayAttributeInfo(GUID_NULL, &second) == E_INVALIDARG, "unknown display attribute accepted");
            ComPtr<ITfThreadMgr> manager;
            success(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager)), "cannot create TSF manager");
            TfClientId client = 0; success(manager->Activate(&client), "TSF manager activation failed");
            check(service->ActivateEx(manager.Get(), client, TF_TMF_SECUREMODE) == E_NOTIMPL, "secure mode should be rejected");
            success(service->Deactivate(), "text service deactivation failed");
            success(service->Deactivate(), "repeated deactivation failed");
            success(manager->Deactivate(), "TSF manager deactivation failed");
        }
        check(unload() == S_OK, "COM objects or subscriptions leaked");
        FreeLibrary(dll); dll = nullptr;
        CoUninitialize(); std::cout << "PASS: " << checks << " TSF COM checks\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        if (dll) FreeLibrary(dll);
        if (SUCCEEDED(apartment)) CoUninitialize(); return 1;
    }
}
