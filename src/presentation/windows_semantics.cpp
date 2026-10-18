module;
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <commctrl.h>
#include <imm.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/TextInputContext.h>
#include <atomic>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.presentation;
import phoenix.navigation;

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
	std::deque<std::string> focuses;
	std::deque<std::pair<std::string, std::string>> values;
	int revision = 0;
	std::weak_ptr<SessionPort> session;
	std::uint64_t generation = 0;
	int focus = -1;
	bool active = true;
	std::string status;
};
std::shared_ptr<Shared> state;
class NativeTextInput final : public Rml::TextInputHandler
{
	void OnActivate(Rml::TextInputContext *input) override
	{
		Rml::Rectanglef bounds;
		if (!state || !input->GetBoundingBox(bounds))
			return;
		const auto window = state->window;
		if (const auto ime = ImmGetContext(window))
		{
			COMPOSITIONFORM composition{};
			composition.dwStyle = CFS_POINT;
			composition.ptCurrentPos = {static_cast<LONG>(bounds.Left()), static_cast<LONG>(bounds.Bottom())};
			ImmSetCompositionWindow(ime, &composition);
			CANDIDATEFORM candidates{};
			candidates.dwStyle = CFS_CANDIDATEPOS;
			candidates.ptCurrentPos = composition.ptCurrentPos;
			ImmSetCandidateWindow(ime, &candidates);
			ImmReleaseContext(window, ime);
		}
	}
} native_text_input;
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
                       public IInvokeProvider,
                       public IValueProvider,
                       public IRangeValueProvider
{
	std::atomic<ULONG> refs_{1};
	std::shared_ptr<Shared> state_;
	int index_;
	int revision_;
	bool valid() const
	{
		return state_->active &&
		       (index_ < 0 || (revision_ == state_->revision && index_ < static_cast<int>(state_->controls.size())));
	}
	IUnknown *marshaler_ = nullptr;
	Provider *make(int index)
	{
		return new Provider(state_, index);
	}

public:
	Provider(std::shared_ptr<Shared> shared, int index)
	    : state_(std::move(shared)), index_(index), revision_(state_->revision)
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
		else if (iid == __uuidof(IValueProvider) && index_ >= 0)
			*object = static_cast<IValueProvider *>(this);
		else if (iid == __uuidof(IRangeValueProvider) && index_ >= 0)
			*object = static_cast<IRangeValueProvider *>(this);
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
			std::lock_guard lock(state_->mutex);
			if (!valid())
				return UIA_E_ELEMENTNOTAVAILABLE;
			if (!state_->controls[index_].read_only && !state_->controls[index_].editable)
			{
				*result = static_cast<IInvokeProvider *>(this);
				AddRef();
			}
		}
		if (index_ >= 0 && id == UIA_ValuePatternId)
		{
			std::lock_guard lock(state_->mutex);
			if (!valid())
				return UIA_E_ELEMENTNOTAVAILABLE;
			if (state_->controls[index_].editable)
			{
				*result = static_cast<IValueProvider *>(this);
				AddRef();
			}
		}
		if (index_ >= 0 && id == UIA_RangeValuePatternId)
		{
			std::lock_guard lock(state_->mutex);
			if (!valid())
				return UIA_E_ELEMENTNOTAVAILABLE;
			if (state_->controls[index_].percentage)
			{
				*result = static_cast<IRangeValueProvider *>(this);
				AddRef();
			}
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *result) override
	{
		if (!result)
			return E_POINTER;
		VariantInit(result);
		std::lock_guard lock(state_->mutex);
		if (!valid())
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (id == UIA_ControlTypePropertyId)
		{
			result->vt = VT_I4;
			result->lVal = index_ < 0                            ? UIA_GroupControlTypeId
			               : state_->controls[index_].read_only  ? UIA_TextControlTypeId
			               : state_->controls[index_].percentage ? UIA_SliderControlTypeId
			               : state_->controls[index_].editable   ? UIA_EditControlTypeId
			                                                     : UIA_ButtonControlTypeId;
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
			if (id == UIA_IsKeyboardFocusablePropertyId && index_ >= 0 && state_->controls[index_].read_only)
				result->boolVal = VARIANT_FALSE;
			if (id == UIA_IsEnabledPropertyId && index_ >= 0)
				result->boolVal = state_->controls[index_].placement.enabled ? VARIANT_TRUE : VARIANT_FALSE;
		}
		else if (id == UIA_HasKeyboardFocusPropertyId)
		{
			result->vt = VT_BOOL;
			result->boolVal = state_->focus == index_ ? VARIANT_TRUE : VARIANT_FALSE;
		}
		else if (id == UIA_HelpTextPropertyId && index_ < 0)
		{
			result->vt = VT_BSTR;
			result->bstrVal = SysAllocString(wide(state_->status).c_str());
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetValue(double value) override
	{
		if (!std::isfinite(value) || value < 0 || value > 100)
			return E_INVALIDARG;
		const auto text = wide(std::to_string(value));
		return SetValue(text.c_str());
	}
	HRESULT STDMETHODCALLTYPE get_Value(double *result) override
	{
		if (!result)
			return E_POINTER;
		std::lock_guard lock(state_->mutex);
		if (!valid() || index_ < 0)
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (!state_->controls[index_].percentage)
			return UIA_E_NOTSUPPORTED;
		*result = std::stod(state_->controls[index_].value);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_Minimum(double *result) override
	{
		if (!result)
			return E_POINTER;
		*result = 0;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_Maximum(double *result) override
	{
		if (!result)
			return E_POINTER;
		*result = 100;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_SmallChange(double *result) override
	{
		if (!result)
			return E_POINTER;
		*result = 1;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_LargeChange(double *result) override
	{
		if (!result)
			return E_POINTER;
		*result = 10;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override
	{
		if (!value)
			return E_INVALIDARG;
		const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
		if (length <= 0 || length > 4097)
			return E_INVALIDARG;
		std::string text(length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), length, nullptr, nullptr);
		text.pop_back();
		std::lock_guard lock(state_->mutex);
		if (!valid() || index_ < 0)
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (!state_->controls[index_].editable)
			return UIA_E_NOTSUPPORTED;
		if (!state_->controls[index_].placement.enabled)
			return UIA_E_ELEMENTNOTENABLED;
		if (state_->values.size() >= 64)
			return E_FAIL;
		state_->values.emplace_back(state_->controls[index_].id, std::move(text));
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_Value(BSTR *result) override
	{
		if (!result)
			return E_POINTER;
		std::lock_guard lock(state_->mutex);
		if (!valid() || index_ < 0)
			return UIA_E_ELEMENTNOTAVAILABLE;
		*result = SysAllocString(wide(state_->controls[index_].value).c_str());
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *result) override
	{
		if (!result)
			return E_POINTER;
		*result = FALSE;
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
		if (!valid())
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
		*result = SafeArrayCreateVector(VT_I4, 0, 4);
		if (!*result)
			return E_OUTOFMEMORY;
		int values[] = {UiaAppendRuntimeId,
		                static_cast<int>(reinterpret_cast<std::uintptr_t>(state_->window) & 0x7fffffff), index_ + 1,
		                revision_};
		for (LONG i = 0; i < 4; ++i)
			SafeArrayPutElement(*result, &i, &values[i]);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *result) override
	{
		if (!result)
			return E_POINTER;
		std::lock_guard lock(state_->mutex);
		if (!valid())
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (index_ >= 0)
		{
			POINT origin{};
			ClientToScreen(state_->window, &origin);
			const auto &box = state_->controls[index_].placement;
			*result = {origin.x + static_cast<double>(box.x), origin.y + static_cast<double>(box.y), box.width,
			           box.height};
			return S_OK;
		}
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
			if (!valid())
				return UIA_E_ELEMENTNOTAVAILABLE;
			if (index_ >= 0 && state_->focuses.size() < 64)
				state_->focuses.push_back(state_->controls[index_].id);
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **result) override
	{
		if (!result)
			return E_POINTER;
		*result = make(-1);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
	                                                   IRawElementProviderFragment **result) override
	{
		if (!result)
			return E_POINTER;
		std::lock_guard lock(state_->mutex);
		if (!state_->active)
			return UIA_E_ELEMENTNOTAVAILABLE;
		POINT origin{};
		ClientToScreen(state_->window, &origin);
		for (int i = static_cast<int>(state_->controls.size()) - 1; i >= 0; --i)
		{
			const auto &box = state_->controls[i].placement;
			if (x >= origin.x + box.x && y >= origin.y + box.y && x < origin.x + box.x + box.width &&
			    y < origin.y + box.y + box.height)
			{
				*result = make(i);
				return S_OK;
			}
		}
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
		if (!valid() || index_ < 0)
			return UIA_E_ELEMENTNOTAVAILABLE;
		if (!state_->controls[index_].placement.enabled)
			return UIA_E_ELEMENTNOTENABLED;
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
	Rml::SetTextInputHandler(&native_text_input);
	state->window = static_cast<HWND>(window);
	root = new Provider(state, -1);
	SetWindowSubclass(state->window, window_proc, 1, 0);
}
void publish_semantics(const Application &app)
{
	if (!state)
		return;
	std::unique_lock lock(state->mutex);
	const auto previous_focus = state->focus;
	const bool announce =
	    (app.console_surface_active ||
	     (app.native_menu && (app.screen == Screen::home || app.screen == Screen::server_lobby ||
	                          app.screen == Screen::client_lobby || app.screen == Screen::settings))) &&
	    app.accessible_status != state->status && !app.accessible_status.empty();
	state->status = app.accessible_status;
	auto controls = navigation_controls(app);
	const bool replaced =
	    state->session.lock() != app.session || (app.session && state->generation != app.session->generation());
	bool changed = replaced || controls.size() != state->controls.size();
	if (replaced)
	{
		state->session = app.session;
		state->generation = app.session ? app.session->generation() : 0;
		state->actions.clear();
		state->focuses.clear();
		state->values.clear();
	}
	for (std::size_t i = 0; !changed && i < controls.size(); ++i)
		changed = controls[i].id != state->controls[i].id;
	if (changed)
		++state->revision;
	state->focus = -1;
	for (std::size_t i = 0; i < controls.size(); ++i)
		if (controls[i].placement.focused)
			state->focus = static_cast<int>(i);
	state->controls = std::move(controls);
	Provider *focused =
	    state->focus >= 0 && (changed || state->focus != previous_focus) ? new Provider(state, state->focus) : nullptr;
	lock.unlock();
	if (announce && root && UiaClientsAreListening())
	{
		auto message = SysAllocString(wide(app.accessible_status).c_str());
		auto activity = SysAllocString(L"Phoenix Console action");
		UiaRaiseNotificationEvent(root, NotificationKind_ActionCompleted, NotificationProcessing_ImportantMostRecent,
		                          message, activity);
		SysFreeString(message);
		SysFreeString(activity);
	}
	if (focused)
	{
		UiaRaiseAutomationEvent(focused, UIA_AutomationFocusChangedEventId);
		focused->Release();
	}
}
void drain_semantic_actions(Application &app)
{
	if (!state)
		return;
	std::deque<std::string> pending;
	std::deque<std::string> focuses;
	std::deque<std::pair<std::string, std::string>> values;
	{
		std::lock_guard lock(state->mutex);
		pending.swap(state->actions);
		focuses.swap(state->focuses);
		values.swap(state->values);
	}
	for (const auto &id : focuses)
		if (app.focus_control)
			app.focus_control(id);
	for (const auto &[id, value] : values)
		if (app.set_control_value)
			app.set_control_value(id, value);
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
	if (app.native_menu)
	{
		bool checked = false;
		const auto controls = navigation_controls(app);
		for (std::size_t i = 0; i < controls.size(); ++i)
		{
			const auto &control = controls[i];
			if (!control.read_only && !control.percentage)
				continue;
			auto *provider = new Provider(state, static_cast<int>(i));
			VARIANT type;
			VariantInit(&type);
			provider->GetPropertyValue(UIA_ControlTypePropertyId, &type);
			UiaRect bounds{};
			provider->get_BoundingRectangle(&bounds);
			bool valid = type.vt == VT_I4 && bounds.width > 0 && bounds.height > 0;
			IUnknown *pattern = nullptr;
			if (control.read_only)
			{
				provider->GetPatternProvider(UIA_InvokePatternId, &pattern);
				valid = valid && type.lVal == UIA_TextControlTypeId && !pattern;
				if (pattern)
					pattern->Release();
			}
			else
			{
				provider->GetPatternProvider(UIA_RangeValuePatternId, &pattern);
				IRangeValueProvider *range = nullptr;
				if (pattern)
				{
					pattern->QueryInterface(__uuidof(IRangeValueProvider), reinterpret_cast<void **>(&range));
					pattern->Release();
				}
				valid = valid && type.lVal == UIA_SliderControlTypeId && range;
				if (range)
				{
					valid = valid && SUCCEEDED(range->SetValue(app.music_volume));
					range->Release();
					drain_semantic_actions(app);
				}
			}
			VariantClear(&type);
			provider->Release();
			if (!valid)
				return false;
			checked = true;
		}
		return checked;
	}
	bool edited = false;
	for (std::size_t i = 0; i < app.screen_inputs.size(); ++i)
	{
		const auto id = app.screen_inputs[i].id;
		auto controls = navigation_controls(app);
		for (std::size_t index = 0; index < controls.size(); ++index)
			if (controls[index].id == id)
			{
				auto *field = new Provider(state, static_cast<int>(index));
				IUnknown *pattern = nullptr;
				field->GetPatternProvider(UIA_ValuePatternId, &pattern);
				field->Release();
				if (!pattern)
					return false;
				IValueProvider *value = nullptr;
				pattern->QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void **>(&value));
				pattern->Release();
				if (!value)
					return false;
				const auto changed = value->SetValue(L"probe-\u03b1");
				value->Release();
				auto original = app.set_control_value;
				app.set_control_value = [&](const std::string &target, const std::string &text)
				{
					const bool accepted = original && original(target, text);
					edited = accepted && target == id && text == "probe-\xce\xb1";
					return accepted;
				};
				drain_semantic_actions(app);
				app.set_control_value = std::move(original);
				if (FAILED(changed))
					return false;
				break;
			}
		if (edited)
			break;
	}
	IRawElementProviderFragment *child = nullptr;
	if (FAILED(root->Navigate(NavigateDirection_FirstChild, &child)) || !child)
		return false;
	UiaRect bounds{};
	const bool placed = SUCCEEDED(child->get_BoundingRectangle(&bounds)) && bounds.width > 0 && bounds.height > 0;
	bool focused = false;
	auto original_focus = app.focus_control;
	app.focus_control = [&](const std::string &id)
	{
		focused = id == "home";
		if (original_focus)
			original_focus(id);
	};
	child->SetFocus();
	drain_semantic_actions(app);
	app.focus_control = std::move(original_focus);
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
	return labelled && placed && focused && edited && SUCCEEDED(result) && app.screen == Screen::home;
}
} // namespace phoenix
