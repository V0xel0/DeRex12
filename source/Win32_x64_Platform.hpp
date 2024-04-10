#pragma once

// Windows 10
#define _WIN32_WINNT 0x0A00
#define NOMINMAX
#define NOMCX
#define NOSERVICE
#define NOHELP
#define WIN32_LEAN_AND_MEAN

#pragma warning( push )
#pragma warning(disable : 4365)
#pragma warning(disable : 4668)
#pragma warning(disable : 5039)
#include <windows.h>
#include <windowsx.h>

#include <timeapi.h>
#pragma warning( pop )

//? Win32 Platform layer implementations, intended to be used with "WINAPI WinMain" only!
//? In order to provide distinction from Microsoft's WinApi functions "Win32" namespace is
//? provided for all custom platform layer functions and structures
//! DO NOT INCLUDE IT ENYWHERE ELSE THAN IN WIN32 ENTRY POINT COMPILATION UNIT FILE!
namespace Win32
{
	struct Platform_State
	{
		u64 total_size;
		void *memory;
	};
	
	struct Platform_Clock
	{
		f32 delta_s;
		s32 target_fps;
		u64 clock_freq;
	};
	
	struct App_DLL
	{
		HMODULE code_dll;
		FILETIME last_change;
		app_update_ptr* update_func;
		b32 is_valid;
	};

	global_variable b32 g_is_running = true;

	LRESULT CALLBACK main_callback(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		LRESULT output = 0;

		switch (message)
		{
			case WM_MENUCHAR:
			{
				output = MAKELRESULT(0, MNC_CLOSE);
			}
			break;

			case WM_DESTROY:
			{
				OutputDebugStringA("Window Destroyed\n");
				PostQuitMessage(0);
			}
			break;

			default:
			{
				output = DefWindowProc(window, message, wParam, lParam);
			}
			break;
		}
		return (output);
	}

	// Creates window for current process, cause of CS_OWNDC device context is assumed to not be shared with anyone
	internal HWND create_window(const s32 w, const s32 h, const char *name)
	{
		HINSTANCE instance = nullptr;
		HWND mainWindow = nullptr;

		instance = GetModuleHandleA(nullptr);
		WNDCLASSEXA windowClass = {};

		windowClass.cbSize = sizeof(WNDCLASSEXA);
		windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		windowClass.lpfnWndProc = Win32::main_callback;
		windowClass.hInstance = instance;
		windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
		windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 6);
		windowClass.lpszClassName = name;

		const s32 error = RegisterClassExA(&windowClass);
		GameAssert(error != 0 && "Class registration failed");

		RECT rc = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
		AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
		const s32 winStyle = WS_SIZEBOX | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_OVERLAPPED | WS_SYSMENU;
		const s32 winStyleEx = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;
		
		mainWindow = CreateWindowExA(
		    winStyleEx,
			name,
			name,
			winStyle,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			instance,
			nullptr);

		GameAssert(mainWindow != nullptr && "Window creation failed");

		ShowWindow(mainWindow, SW_SHOW);
		SetForegroundWindow(mainWindow);
		SetFocus(mainWindow);
		ShowCursor(true);
		ShowScrollBar(mainWindow, SB_BOTH, FALSE);
		return mainWindow;
	}
	
	internal auto get_window_client_dims(HWND window)
	{
		struct Output
		{
			u32 w; u32 h;
		} out;
		
		RECT rect;
		GetClientRect(window, &rect);
		out.w = rect.right - rect.left;
		out.h = rect.bottom - rect.top;
		return out;
	}
	
	// ===============================================================================================================================
	// =========================================== MOUSE & KEYBOARD EVENTS HANDLING ==================================================
	// ===============================================================================================================================
	internal void process_keyboard_mouse_event(Game_Key_State *newState, const b32 isDown)
	{
		if (newState->wasDown != isDown)
		{
			newState->wasDown = isDown;
			++newState->halfTransCount;
		}
	}

	internal void process_keyboard_mouse_msg(MSG *msg, Game_Controller *keyboard_mouse)
	{
		LPARAM l_param = msg->lParam;
		WPARAM w_param = msg->wParam;
		
		u32 vk_code = (u32)w_param;

		switch (msg->message)
		{
			// Used for obtaining relative mouse movement and wheel without acceleration and buttons state
			case WM_INPUT:
			{
				u32 size = sizeof(RAWINPUT);
				RAWINPUT raw[sizeof(RAWINPUT)];
				u32 copied = GetRawInputData((HRAWINPUT)l_param, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));

				if (copied != size)
				{
					MessageBoxA(NULL, "Incorrect raw input data size!", "error", 0);
					break;
				}
				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					keyboard_mouse->mouse.delta_x = raw->data.mouse.lLastX;
					keyboard_mouse->mouse.delta_y = raw->data.mouse.lLastY;
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
					{
						keyboard_mouse->mouse.delta_wheel = (*(s16 *)&raw->data.mouse.usButtonData);
					}
					// Mouse buttons
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->rotate_start, true);
					}
					if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->rotate_start, false);
					}
				}
			}
			break;

			case WM_MOUSEMOVE:
			{
				keyboard_mouse->mouse.x = (s32)(GET_X_LPARAM(l_param) );
				keyboard_mouse->mouse.y = (s32)(GET_Y_LPARAM(l_param) );
			}
			break;

			// Keyboard input messages
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				b32 alt_was_down = TestBit(l_param, 29);
				b32 was_down = TestBit(l_param, 30) != 0;
				b32 is_down = TestBit(l_param, 31) == 0;
				
				if (was_down != is_down)
				{
					if (vk_code == 'W')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_forward, is_down);
					}
					else if (vk_code == 'S')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_backward, is_down);
					}
					else if (vk_code == 'A')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_left, is_down);
					}
					else if (vk_code == 'D')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_right, is_down);
					}
					else if (vk_code == 'Q')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action1, is_down);
					}
					else if (vk_code == 'E')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action2, is_down);
					}
					else if (vk_code == 'Z')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action3, is_down);
					}
					else if (vk_code == 'X')
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action4, is_down);
					}
					else if (vk_code == VK_ESCAPE)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->back, is_down);
					}
					else if (vk_code == VK_RETURN)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->start, is_down);
					}
					else if (vk_code == VK_SPACE)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->main_click, is_down);
					}
					else if (vk_code == VK_UP)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action1, is_down);
					}
					else if (vk_code == VK_DOWN)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->action2, is_down);
					}
					else if (vk_code == VK_LEFT)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_left, is_down);
					}
					else if (vk_code == VK_RIGHT)
					{
						Win32::process_keyboard_mouse_event(&keyboard_mouse->move_right, is_down);
					}
				}
				if ((vk_code == VK_F4) && alt_was_down)
				{
					Win32::g_is_running = false;
				}
				break;
			}
			default:
			{
			}
			break;
		}
	}

	internal void register_mouse_raw_input(HWND window = nullptr)
	{
		RAWINPUTDEVICE raw_devices[1];
		// Mouse registering info
		raw_devices[0].usUsagePage = 0x01;
		raw_devices[0].usUsage = 0x02;
		raw_devices[0].dwFlags = 0;
		raw_devices[0].hwndTarget = window;

		if (RegisterRawInputDevices(raw_devices, 1, sizeof(raw_devices[0])) == FALSE)
		{
			MessageBoxA(NULL, "Could not register mouse and/or keyboard for raw input", "error", 0);
			GameAssert(0);
		}
	}

	internal s32 get_monitor_freq()
	{
		DEVMODEA dev_info = {};
		dev_info.dmSize = sizeof(DEVMODE);
		EnumDisplaySettingsA(0, ENUM_CURRENT_SETTINGS, &dev_info);
		return (s32)dev_info.dmDisplayFrequency;
	}
	
	// ===============================================================================================================================
	// ========================================================= TIMERS ==============================================================
	// ===============================================================================================================================
	internal Platform_Clock clock_create(s32 fps = 60)
	{
		Platform_Clock out{ .target_fps = 60};
		LARGE_INTEGER temp{};
		QueryPerformanceFrequency(&temp);
		out.clock_freq = temp.QuadPart;
		return out;
	}
	
	internal f64 get_elapsed_seconds_here(const Platform_Clock &clock, u64 tick_start)
	{
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&end);
		return (f64)(end.QuadPart - tick_start) / clock.clock_freq;
	}
	
	internal f64 get_elapsed_ms_here(const Platform_Clock &clock, u64 tick_start)
	{
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&end);
		return (f64)((end.QuadPart - tick_start) * 1000) / clock.clock_freq;
	}
	
	internal void clock_update_and_wait(Platform_Clock &clock, u64 tick_start)
	{
		f64 time_work_s = get_elapsed_seconds_here(clock, tick_start);
		f64 target_s = 1.0 / (f64)clock.target_fps;
		f64 time_to_wait_s = target_s - time_work_s;

		if (time_to_wait_s > 0 && time_to_wait_s < target_s)
		{
			Sleep((DWORD)(time_to_wait_s * 1000));
			time_to_wait_s = get_elapsed_seconds_here(clock, tick_start);
			while (time_to_wait_s < target_s)
				time_to_wait_s = get_elapsed_seconds_here(clock, tick_start);
		}

		clock.delta_s = (f32)get_elapsed_seconds_here(clock, tick_start);
	}
	
	internal u64 get_performance_ticks()
	{
		LARGE_INTEGER end{};
		QueryPerformanceCounter(&end);
		return end.QuadPart;
	}
	
	// ===============================================================================================================================
	// ================================================= DEBUG INTERNAL FUNCTIONS ====================================================
	// ===============================================================================================================================
#if 0
	Debug_File_Output debug_read_file(const char *fileName)
	{
		HANDLE fileHandle = CreateFileA(fileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
		auto d = defer([&]
		               {
		               CloseHandle(fileHandle);
					   });
		Debug_File_Output out = {};

		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			LARGE_INTEGER fileSize;

			if (GetFileSizeEx(fileHandle, &fileSize))
			{
				u32 fileSize32 = trunc_u64_to_u32((u64)fileSize.QuadPart);
				out.data = VirtualAlloc(0, fileSize32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

				if (out.data)
				{
					DWORD bytesRead;
					if (ReadFile(fileHandle, out.data, fileSize32, &bytesRead, 0) && (fileSize32 == bytesRead))
					{
						out.dataSize = fileSize32;
					}
					else
					{
						VirtualFree(out.data, 0, MEM_RELEASE);
						out.data = nullptr;
						//TODO: Log
					}
				}
			}
		}
		else
		{
			//TODO: Log
		}

		return (out);
	}

	b32 debug_write_file(const char *fileName, void *memory, u32 memSize)
	{
		b32 isSuccessful = false;
		HANDLE fileHandle = CreateFileA(fileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
		auto d = defer([&]
		               {
		               CloseHandle(fileHandle);
					   });

		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			DWORD bytesWritten;
			if (WriteFile(fileHandle, memory, memSize, &bytesWritten, 0))
			{
				isSuccessful = (bytesWritten == memSize);
			}
			else
			{
				//TODO: Log
			}
		}
		else
		{
			//TODO: Log
		}

		return isSuccessful;
	}

	void debug_free_file_memory(void *memory)
	{
		if (memory != nullptr)
		{
			VirtualFree(memory, 0, MEM_RELEASE);
		}
	}
#endif
	
	internal FILETIME get_file_write_time(const char *name)
	{
		FILETIME out{};
		
		WIN32_FILE_ATTRIBUTE_DATA data;
		if (GetFileAttributesExA(name, GetFileExInfoStandard, &data ))
		{
			out = data.ftLastWriteTime;
		}
		
		return out;
	}
	
	// ===============================================================================================================================
	// ================================================= HOT RELOAD ====================================================
	// ===============================================================================================================================
	
	internal App_DLL load_app_dll(const char* dll_path, const char* temp_dll_path)
	{
		App_DLL out{};
		
		out.last_change = Win32::get_file_write_time(dll_path);
		CopyFileA(dll_path, temp_dll_path, FALSE);
		out.code_dll = LoadLibraryA(temp_dll_path);

		if(out.code_dll)
		{
			out.update_func = (app_update_ptr *)GetProcAddress(out.code_dll, "app_full_update");
			out.is_valid = (out.update_func != nullptr);
		}
		AlwaysAssert(out.update_func != 0 && "Loading dll failed!");
		
		return out;
	}
	
	internal void unload_app_dll(App_DLL* app_code)
	{
		AlwaysAssert(app_code != 0 && "Trying to free invalid game code!");
		if(app_code->code_dll)
		{
			FreeLibrary(app_code->code_dll);
			app_code->code_dll = nullptr;
		}
		app_code->is_valid = false;
		app_code->update_func = nullptr;
	}

} // namespace Win32