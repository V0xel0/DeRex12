//? This Translation Unit provides data and services (declared by application) from OS to a game/app layer
//? Architecture style is inspired from "Handmade Hero" series by Casey Muratori:
//? Application is treated as a service that OS fulfills,
//? instead of abstracting platform code and handling it as kind of "Virtual OS"
//? In case of porting to a different platform, this is the ONLY file you need to change

#include <cstdio>

#include "Utils.hpp"
#include "GameAsserts.hpp"
#include "Game_Services.hpp"
#include "Win32_x64_Platform.hpp"

#include "Allocators.hpp"
#include "Views.hpp"
#include "Math.hpp"

#ifdef _DEBUG
#else

#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 611;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = "..\\external\\D3D12\\"; }

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	SYSTEM_INFO windows_info{};
	GetSystemInfo(&windows_info);
	auto cores_count = windows_info.dwNumberOfProcessors;
	AlwaysAssert(windows_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 
	             && "This is not a 64-bit OS!");
	
	Win32::Platform_Clock clock = Win32::clock_create();
	UINT schedulerGranularity = 1;
	b32 schedulerError = (timeBeginPeriod(schedulerGranularity) == TIMERR_NOERROR);
	
	Win32::register_mouse_raw_input();
	HWND win_handle = Win32::create_window(1280, 720, "Raster");
	auto&& [width, height] = Win32::get_window_client_dims(win_handle);
	
	Alloc_Arena global_memory
	{
		.max_size = GiB(2),
		.base = (byte*)VirtualAlloc(0, GiB(2), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
	};
	AlwaysAssert(global_memory.base && "Failed to allocate memory from Windows");
	
	
//	if (!dx_init_resources(&machine, &context, win_handle))
//		return 0;
	
	Game_Input gameInputBuffer[2] = {};
	Game_Input *newInputs = &gameInputBuffer[0];
	Game_Input *oldInputs = &gameInputBuffer[1];
	
	while (Win32::g_is_running)
	{
		u64 tick_start = Win32::get_performance_ticks();
		static u32 counter;
		// Input handling
		Game_Controller *oldKeyboardMouseController = get_game_controller(oldInputs, 0);
		Game_Controller *newKeyboardMouseController = get_game_controller(newInputs, 0);
		*newKeyboardMouseController = {};
		newKeyboardMouseController->isConnected = true;
		for (u32 i = 0; i < array_count_32(newKeyboardMouseController->buttons); ++i)
		{
			newKeyboardMouseController->buttons[i].wasDown = oldKeyboardMouseController->buttons[i].wasDown;
			newKeyboardMouseController->mouse.x = oldKeyboardMouseController->mouse.x;
			newKeyboardMouseController->mouse.y = oldKeyboardMouseController->mouse.y;
		}
		// Windows message loop
		MSG msg = {};
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			Win32::process_keyboard_mouse_msg(&msg, newKeyboardMouseController);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				Win32::g_is_running = false;
				break;
			}
		}
		
		auto&& [new_width, new_height] = Win32::get_window_client_dims(win_handle);
		
		
		f64 frame_time_ms = Win32::get_elapsed_ms_here(clock, tick_start);
		
		if (counter % 100 == 0)
		{
			char time_buf[32];
			sprintf_s(time_buf, sizeof(time_buf), "%.3lf ms", frame_time_ms);
			SetWindowText(win_handle, time_buf);
		}
	}
	
	UnregisterClassA("Raster", GetModuleHandle(nullptr));
	return 0;
}