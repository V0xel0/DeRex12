#include <cstdio>

#include "Utils.hpp"
#include "Allocators.hpp"
#include "Views.hpp"

#include "GameAsserts.hpp"
#include "Game_Services.hpp"
#include "Render_Data.hpp"
#include "Win32_x64_Platform.hpp"

#ifdef _DEBUG
#else

#endif

extern void rhi_run(Data_To_RHI*, Game_Window*);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	const char* app_dll_path 			= "../build/App.dll";
	const char* app_dll_temp_path = "../build/AppTemp.dll";
	
	SYSTEM_INFO windows_info{};
	GetSystemInfo(&windows_info);
	auto cores_count = windows_info.dwNumberOfProcessors;
	AlwaysAssert(windows_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 
	             && "This is not a 64-bit OS!");
	
	Win32::Platform_Clock clock = Win32::clock_create();
	UINT schedulerGranularity = 1;
	b32 schedulerError = (timeBeginPeriod(schedulerGranularity) == TIMERR_NOERROR);

	Win32::register_mouse_raw_input();
	
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	HWND win_handle = Win32::create_window(1280, 720, "DeRex12");
	auto&& [width, height] = Win32::get_window_client_dims(win_handle);
	
	Alloc_Arena platform_arena
	{
		.max_size = MiB(2150),
		.base = (byte*)VirtualAlloc(0, MiB(2150), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
	};
	AlwaysAssert(platform_arena.base && "Failed to allocate memory from Windows");
	
	// Fill game services needed by application
	Game_Memory game_memory
	{
		.is_initalized = false,
		.size_permanent_storage = MiB(64),
		.permanent_storage = allocate(&platform_arena, MiB(64)),
		.size_transient_storage = GiB(2),
		.transient_storage = allocate(&platform_arena, GiB(2))
	};
	
	Game_Window game_window { (void*)win_handle, 0.0f, width, height };
	
	Game_Input game_input_buffer[2] = {};
	Game_Input* new_inputs = &game_input_buffer[0];
	Game_Input* old_inputs = &game_input_buffer[1];
	
	Win32::App_DLL app_code = Win32::load_app_dll(app_dll_path, app_dll_temp_path);
	u64 ticks_loop_start = Win32::get_performance_ticks();
	
	while (Win32::g_is_running)
	{
		u64 tick_start = Win32::get_performance_ticks();
		static u32 counter;
		
		// Check if there is new app dll to update
		FILETIME app_dll_new_time = Win32::get_file_write_time(app_dll_path);
		if(CompareFileTime(&app_dll_new_time, &app_code.last_change) != 0)
		{	
			Win32::unload_app_dll(&app_code);
			app_code = Win32::load_app_dll(app_dll_path, app_dll_temp_path);
		}
		
		// Input handling
		Game_Controller* oldKeyboardMouseController = get_game_controller(old_inputs, 0);
		Game_Controller* newKeyboardMouseController = get_game_controller(new_inputs, 0);
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
		
		// Window size change check
		auto&& [new_width, new_height] = Win32::get_window_client_dims(win_handle);
		
		// Update application
		game_window.width = new_width;
		game_window.height = new_height;
		game_window.is_closed = Win32::g_is_running ? false : true;
		game_window.time_ms = Win32::get_elapsed_ms_here(clock, ticks_loop_start);
		
		if (Win32::g_is_running)
		{
			// Call application
			Data_To_RHI* rhi_data = app_code.update_func(&game_memory, &game_window, new_inputs);
			// Call RHI
			rhi_run(rhi_data, &game_window);
		}

		f64 frame_time_ms = Win32::get_elapsed_ms_here(clock, tick_start);
		swap(old_inputs, new_inputs);
		
		if (counter % 100 == 0)
		{
			char time_buf[32];
			sprintf_s(time_buf, sizeof(time_buf), "%.3lf ms", frame_time_ms);
			SetWindowText(win_handle, time_buf); //WARNING - this might stall with very high fps (0.001ms)!
		}
	}

	UnregisterClassA("DeRex12", GetModuleHandle(nullptr));
	return 0;
}