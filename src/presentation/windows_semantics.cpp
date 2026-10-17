#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <commctrl.h>
#include <phoenix/semantics.hpp>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

namespace phoenix
{
namespace
{
struct Shared
{
	HWND window{};
	std::mutex mutex;
	std::vector<SemanticControl> controls;
	std::deque<std::string> actions;
	int focus = -1;
	bool active = true;
};
std::shared_ptr<Shared> state;
std::wstring wide(const std::string &text)
{
	int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(count, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
	return result;
}
class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public IInvokeProvider
{
	std::atomic<ULONG> refs_{1};
	std::shared_ptr<Shared> state_;
	int index_;
	IUnknown *marshaler_ = nullptr;
	Provider *make(int index)
	{
		return new Provider(state_, index);
	}

public:
	Provider(std::shared_ptr<Shared> shared, int index) : state_(std::move(shared)), index_(index)
	{
		CoCreateFreeThreadedMarshaler(static_cast<IRawElementProviderSimple *>(this), &marshaler_);
	}
	~Provider()
	{
		if (marshaler_)
			marshaler_->Release();
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
	{
		if (!object)
			return E_POINTER;
		*object = nullptr;
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
			*object = static_cast<IRawElementProviderSimple *>(this);
		else if (iid == __uuidof(IRawElementProviderFragment))
			*object = static_cast<IRawElementProviderFragment *>(this);
		else if (iid == __uuidof(IRawElementProviderFragmentRoot) && index_ == -1)
			*object = static_cast<IRawElementProviderFragmentRoot *>(this);
		else if (iid == __uuidof(IInvokeProvider) && index_ >= 0)
			*object = static_cast<IInvokeProvider *>(this);
		else if (iid == __uuidof(IMarshal) && marshaler_)
			return marshaler_->QueryInterface(iid, object);
		if (!*object)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++refs_;
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		auto refs = --refs_;
		if (!refs)
			delete this;
		return refs;
	}
	HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *result) override
	{
		if (!result)
			return E_POINTER;
		*result = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown **result) override
	{
		if (!result)
			return E_POINTER;
		*result = nullptr;
		if (index_ >= 0 && id == UIA_InvokePatternId)
		{
			*result = static_cast<IInvokeProvider *>(this);
			AddRef();
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *result) override
	{
		if (!result)
			return E_POINTER;
		VariantInit(result);
		std::lock_guard lock(state_->mutex);
		if (!state_->active)
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (index_ >= static_cast<int>(state_->controls.size()))
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (id == UIA_ControlTypePropertyId)
		{
			result->vt = VT_I4;
			result->lVal = index_ < 0 ? UIA_GroupControlTypeId : UIA_ButtonControlTypeId;
		}
		else if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId)
		{
			auto text = index_ < 0                 ? std::string("Phoenix navigation")
			            : id == UIA_NamePropertyId ? state_->controls[index_].label
			                                       : state_->controls[index_].id;
			result->vt = VT_BSTR;
			result->bstrVal = SysAllocString(wide(text).c_str());
		}
		else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
		         id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId)
		{
			result->vt = VT_BOOL;
			result->boolVal = VARIANT_TRUE;
		}
		else if (id == UIA_HasKeyboardFocusPropertyId)
		{
			result->vt = VT_BOOL;
			result->boolVal = state_->focus == index_ ? VARIANT_TRUE : VARIANT_FALSE;
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **result) override
	{
		if (!result)
			return E_POINTER;
		*result = nullptr;
		return index_ == -1 ? UiaHostProviderFromHwnd(state_->window, result) : S_OK;
	}
	HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment **result) override
	{
		if (!result)
			return E_POINTER;
		*result = nullptr;
		std::lock_guard lock(state_->mutex);
		if (!state_->active)
			return UIA_E_ELEMENTNOTAVAILABLE;
		int count = static_cast<int>(state_->controls.size());
		if (index_ < 0 && count && direction == NavigateDirection_FirstChild)
			*result = make(0);
		else if (index_ < 0 && count && direction == NavigateDirection_LastChild)
			*result = make(count - 1);
		else if (index_ >= 0 && direction == NavigateDirection_Parent)
			*result = make(-1);
		else if (index_ >= 0 && direction == NavigateDirection_NextSibling && index_ + 1 < count)
			*result = make(index_ + 1);
		else if (index_ > 0 && direction == NavigateDirection_PreviousSibling)
			*result = make(index_ - 1);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **result) override
	{
		if (!result)
			return E_POINTER;
		*result = SafeArrayCreateVector(VT_I4, 0, 3);
		if (!*result)
			return E_OUTOFMEMORY;
		int values[] = {UiaAppendRuntimeId,
		                static_cast<int>(reinterpret_cast<std::uintptr_t>(state_->window) & 0x7fffffff), index_ + 1};
		for (LONG i = 0; i < 3; ++i)
			SafeArrayPutElement(*result, &i, &values[i]);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *result) override
	{
		if (!result)
			return E_POINTER;
		RECT rect{};
		GetWindowRect(state_->window, &rect);
		*result = {static_cast<double>(rect.left), static_cast<double>(rect.top),
		           static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **result) override
	{
		if (!result)
			return E_POINTER;
		*result = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetFocus() override
	{
		{
			std::lock_guard lock(state_->mutex);
			if (!state_->active)
				return UIA_E_ELEMENTNOTAVAILABLE;
			state_->focus = index_;
		}
		UiaRaiseAutomationEvent(this, UIA_AutomationFocusChangedEventId);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **result) override
	{
		if (!result)
			return E_POINTER;
		*result = make(-1);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double, double, IRawElementProviderFragment **result) override
	{
		if (!result)
			return E_POINTER;
		*result = make(-1);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **result) override
	{
		if (!result)
			return E_POINTER;
		std::lock_guard lock(state_->mutex);
		*result = make(state_->focus);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Invoke() override
	{
		std::lock_guard lock(state_->mutex);
		if (!state_->active || index_ < 0 || index_ >= static_cast<int>(state_->controls.size()))
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (state_->actions.size() < 64)
			state_->actions.push_back(state_->controls[index_].id);
		return S_OK;
	}
};
Provider *root = nullptr;
bool com_initialised = false;
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
	if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == UiaRootObjectId && root)
		return UiaReturnRawElementProvider(window, wparam, lparam, root);
	return DefSubclassProc(window, message, wparam, lparam);
}
} // namespace
void install_native_accessibility(void *window)
{
	auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	com_initialised = SUCCEEDED(result);
	state = std::make_shared<Shared>();
	state->window = static_cast<HWND>(window);
	root = new Provider(state, -1);
	SetWindowSubclass(state->window, window_proc, 1, 0);
}
void publish_semantics(const Application &app)
{
	if (!state)
		return;
	std::lock_guard lock(state->mutex);
	state->controls = navigation_controls(app);
}
void drain_semantic_actions(Application &app)
{
	if (!state)
		return;
	std::deque<std::string> pending;
	{
		std::lock_guard lock(state->mutex);
		pending.swap(state->actions);
	}
	for (const auto &action : pending)
		invoke_navigation(app, action);
}
void remove_native_accessibility(void *window)
{
	RemoveWindowSubclass(static_cast<HWND>(window), window_proc, 1);
	if (state)
	{
		std::lock_guard lock(state->mutex);
		state->active = false;
	}
	if (root)
	{
		root->Release();
		root = nullptr;
	}
	state.reset();
	if (com_initialised)
		CoUninitialize();
}
bool probe_native_accessibility(Application &app)
{
	publish_semantics(app);
	IRawElementProviderFragment *child = nullptr;
	if (FAILED(root->Navigate(NavigateDirection_FirstChild, &child)) || !child)
		return false;
	IRawElementProviderSimple *simple = nullptr;
	child->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void **>(&simple));
	child->Release();
	if (!simple)
		return false;
	VARIANT name;
	VariantInit(&name);
	simple->GetPropertyValue(UIA_NamePropertyId, &name);
	bool labelled = name.vt == VT_BSTR && name.bstrVal && SysStringLen(name.bstrVal) > 0;
	VariantClear(&name);
	IUnknown *pattern = nullptr;
	simple->GetPatternProvider(UIA_InvokePatternId, &pattern);
	simple->Release();
	if (!pattern)
		return false;
	IInvokeProvider *invoke = nullptr;
	pattern->QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void **>(&invoke));
	pattern->Release();
	if (!invoke)
		return false;
	auto result = invoke->Invoke();
	invoke->Release();
	drain_semantic_actions(app);
	return labelled && SUCCEEDED(result) && app.screen == Screen::home;
}
} // namespace phoenix
