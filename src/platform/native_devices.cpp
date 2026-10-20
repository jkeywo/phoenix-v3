module;
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#endif
#include <SDL3/SDL.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

module phoenix.devices;
namespace phoenix
{
namespace
{
std::vector<DeviceEvent> pending;
#ifdef _WIN32
std::map<UINT32, POINT> contacts;
#endif
std::map<SDL_JoystickID, SDL_Gamepad *> pads;
#ifdef _WIN32
void cancel_contacts()
{
	for (const auto &[id, point] : contacts)
		pending.push_back({DeviceEventKind::Cancel, id, static_cast<float>(point.x), static_cast<float>(point.y)});
	contacts.clear();
}
LRESULT CALLBACK device_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR)
{
	if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP ||
	    message == WM_POINTERCAPTURECHANGED)
	{
		const auto id = GET_POINTERID_WPARAM(wparam);
		POINTER_INFO info{};
		if (message == WM_POINTERCAPTURECHANGED)
		{
			if (auto found = contacts.find(id); found != contacts.end())
			{
				pending.push_back({DeviceEventKind::Cancel, id, static_cast<float>(found->second.x),
				                   static_cast<float>(found->second.y)});
				contacts.erase(found);
			}
		}
		else if (GetPointerInfo(id, &info) && (info.pointerType == PT_TOUCH || info.pointerType == PT_PEN))
		{
			auto point = info.ptPixelLocation;
			ScreenToClient(window, &point);
			auto kind = message == WM_POINTERDOWN ? DeviceEventKind::Down
			            : message == WM_POINTERUP ? DeviceEventKind::Up
			                                      : DeviceEventKind::Move;
			if (info.pointerFlags & POINTER_FLAG_CANCELED)
				kind = DeviceEventKind::Cancel;
			if (kind == DeviceEventKind::Down || contacts.contains(id))
			{
				pending.push_back({kind, id, static_cast<float>(point.x), static_cast<float>(point.y)});
				if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
					contacts.erase(id);
				else
					contacts[id] = point;
			}
			return 0;
		}
	}
	if (message == WM_KILLFOCUS || message == WM_CANCELMODE)
		cancel_contacts();
	// Prevent Windows' synthetic mouse stream from invoking a touch action twice.
	if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST &&
	    (static_cast<ULONG_PTR>(GetMessageExtraInfo()) & 0xffffff00) == 0xff515700)
		return 0;
	return DefSubclassProc(window, message, wparam, lparam);
}
#endif
#ifndef _WIN32
std::mutex touch_mutex;
std::vector<SDL_TouchFingerEvent> touch_events;
bool SDLCALL touch_watch(void *, SDL_Event *event)
{
	if (event->type == SDL_EVENT_FINGER_DOWN || event->type == SDL_EVENT_FINGER_MOTION ||
	    event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED)
	{
		// SDL watches can run on the thread that posts an event. Window queries
		// and the presentation queue stay on the main thread in poll_devices.
		std::lock_guard lock(touch_mutex);
		if (touch_events.size() < 4096)
			touch_events.push_back(event->tfinger);
	}
	return true;
}
#endif
void add(SDL_JoystickID id)
{
	if (pads.contains(id))
		return;
	if (auto *pad = SDL_OpenGamepad(id))
	{
		pads[id] = pad;
		pending.push_back({DeviceEventKind::Added, 0, 0, 0, std::to_string(id)});
	}
}
} // namespace
void open_devices(void *window)
{
#ifndef _WIN32
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_AddEventWatch(touch_watch, nullptr);
#endif
#ifdef _WIN32
	std::cout << "Pointer hardware: " << GetSystemMetrics(SM_MAXIMUMTOUCHES) << " maximum simultaneous contacts\n";
	UINT32 pointer_count = 0;
	if (GetPointerDevices(&pointer_count, nullptr) && pointer_count <= 64)
	{
		std::vector<POINTER_DEVICE_INFO> devices(pointer_count);
		if (GetPointerDevices(&pointer_count, devices.data()))
			for (const auto &device : devices)
			{
				RECT raw{}, display{};
				if (device.pointerDeviceType == POINTER_DEVICE_TYPE_TOUCH &&
				    GetPointerDeviceRects(device.device, &raw, &display))
					std::cout << "Touch display desktop rectangle: " << display.left << ',' << display.top << " / "
					          << display.right - display.left << 'x' << display.bottom - display.top << '\n';
			}
	}
#endif
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
	if (!SDL_Init(SDL_INIT_GAMEPAD))
		throw std::runtime_error(SDL_GetError());
	int count = 0;
	auto *ids = SDL_GetGamepads(&count);
	std::cout << "SDL controllers: " << count << '\n';
	for (int i = 0; i < count; ++i)
	{
		add(ids[i]);
		if (pads.contains(ids[i]))
			std::cout << "Controller " << ids[i] << ": " << SDL_GetGamepadName(pads.at(ids[i])) << '\n';
	}
	SDL_free(ids);
#ifdef _WIN32
	if (!SetWindowSubclass(static_cast<HWND>(window), device_proc, 2, 0))
		throw std::runtime_error("Cannot install pointer input routing");
#endif
}
void close_devices(void *window)
{
#ifndef _WIN32
	SDL_RemoveEventWatch(touch_watch, nullptr);
	{
		std::lock_guard lock(touch_mutex);
		touch_events.clear();
	}
#endif
#ifdef _WIN32
	RemoveWindowSubclass(static_cast<HWND>(window), device_proc, 2);
	cancel_contacts();
#endif
	for (const auto &[id, pad] : pads)
		SDL_CloseGamepad(pad);
	pads.clear();
	pending.clear();
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
std::vector<DeviceEvent> poll_devices()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_GAMEPAD_ADDED)
			add(event.gdevice.which);
		if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
		{
			auto found = pads.find(event.gdevice.which);
			if (found != pads.end())
			{
				pending.push_back({DeviceEventKind::Removed, 0, 0, 0, std::to_string(found->first)});
				SDL_CloseGamepad(found->second);
				pads.erase(found);
			}
		}
		if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
		{
			const char *control = nullptr;
			switch (event.gbutton.button)
			{
			case SDL_GAMEPAD_BUTTON_DPAD_UP:
				control = "game.faster";
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
				control = "game.slower";
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
				control = "game.left";
				break;
			case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
				control = "game.right";
				break;
			case SDL_GAMEPAD_BUTTON_WEST:
				control = "game.manual";
				break;
			case SDL_GAMEPAD_BUTTON_NORTH:
				control = "game.camera";
				break;
			case SDL_GAMEPAD_BUTTON_EAST:
				control = "game.camera-back";
				break;
			case SDL_GAMEPAD_BUTTON_SOUTH:
				control = "game.contacts";
				break;
			}
			if (control)
				pending.push_back({DeviceEventKind::Gamepad, 0, 0, 0, std::to_string(event.gbutton.which), control,
				                   event.gbutton.down ? 1.f : 0.f});
		}
	}
#ifndef _WIN32
	std::vector<SDL_TouchFingerEvent> fingers;
	{
		std::lock_guard lock(touch_mutex);
		fingers.swap(touch_events);
	}
	for (const auto &finger : fingers)
	{
		int width = 0, height = 0;
		SDL_GetWindowSize(SDL_GetWindowFromID(finger.windowID), &width, &height);
		const auto kind = finger.type == SDL_EVENT_FINGER_DOWN     ? DeviceEventKind::Down
		                  : finger.type == SDL_EVENT_FINGER_MOTION ? DeviceEventKind::Move
		                  : finger.type == SDL_EVENT_FINGER_UP     ? DeviceEventKind::Up
		                                                           : DeviceEventKind::Cancel;
		pending.push_back({kind, static_cast<std::uint64_t>(finger.fingerID), finger.x * width, finger.y * height});
	}
#endif
	// Sample each frame so assignment/focus recovery can require a real centred stick.
	for (const auto &[id, pad] : pads)
		for (const auto axis : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY})
		{
			const auto raw = SDL_GetGamepadAxis(pad, axis);
			const float value = raw < 0 ? raw / 32768.f : raw / 32767.f;
			pending.push_back({DeviceEventKind::Gamepad, 0, 0, 0, std::to_string(id),
			                   axis == SDL_GAMEPAD_AXIS_LEFTX ? "flight.yaw" : "flight.thrust", -value});
		}
