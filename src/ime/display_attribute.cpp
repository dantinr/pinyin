#include "common.hpp"

namespace pinyin::ime {
namespace {
TF_DISPLAYATTRIBUTE default_attribute() {
    TF_DISPLAYATTRIBUTE value{};
    value.crText.type = TF_CT_NONE; value.crBk.type = TF_CT_NONE;
    value.lsStyle = TF_LS_SOLID; value.fBoldLine = FALSE;
    value.crLine.type = TF_CT_SYSCOLOR; value.crLine.nIndex = COLOR_WINDOWTEXT;
    value.bAttr = TF_ATTR_INPUT; return value;
}
class Attribute final : public ITfDisplayAttributeInfo, private ModuleObject {
    std::atomic<ULONG> refs_{1};
    TF_DISPLAYATTRIBUTE value_ = default_attribute();
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfDisplayAttributeInfo) return E_NOINTERFACE;
        *result = static_cast<ITfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* result) override { if (!result) return E_POINTER; *result = attribute_id; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* result) override {
        if (!result) return E_POINTER; *result = SysAllocString(L"隐私拼音组合文本"); return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* result) override {
        if (!result) return E_POINTER; *result = value_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE* value) override {
        if (!value) return E_POINTER; value_ = *value; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { value_ = default_attribute(); return S_OK; }
};
class Attributes final : public IEnumTfDisplayAttributeInfo, private ModuleObject {
    std::atomic<ULONG> refs_{1};
    bool consumed_ = false;
public:
    explicit Attributes(bool consumed = false) : consumed_(consumed) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumTfDisplayAttributeInfo) return E_NOINTERFACE;
        *result = static_cast<IEnumTfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        return protect([&] { *result = new Attributes(consumed_); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, ITfDisplayAttributeInfo** result, ULONG* fetched) override {
        if (!result || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        if (!count) return S_OK;
        *result = nullptr;
        if (consumed_) return S_FALSE;
        const auto hr = create_attribute(result);
        if (FAILED(hr)) return hr;
        consumed_ = true; if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { consumed_ = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        if (!count) return S_OK;
        if (consumed_) return S_FALSE;
        consumed_ = true; return count == 1 ? S_OK : S_FALSE;
    }
};
}
HRESULT create_attribute(ITfDisplayAttributeInfo** result) {
    if (!result) return E_POINTER; *result = nullptr;
    return protect([&] { *result = new Attribute; return S_OK; });
}
HRESULT create_attribute_enumerator(IEnumTfDisplayAttributeInfo** result) {
    if (!result) return E_POINTER; *result = nullptr;
    return protect([&] { *result = new Attributes; return S_OK; });
}
}
