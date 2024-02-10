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

#include "D3D12MemAlloc.h"
#include <dxcapi.h>       
#include <d3d12shader.h>

#include "Allocators.hpp"
#include "Views.hpp"
#include "Math.hpp"

#ifdef _DEBUG
#else

#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 611;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = "..\\external\\D3D12\\"; }


#define THR(hr) AlwaysAssert(SUCCEEDED(hr));
#define RELEASE_SAFE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

[[nodiscard]]
u64 signal_and_increment(ID3D12CommandQueue* command_queue, ID3D12Fence* fence, u64 value)
{
	u64 value_to_signal = ++value;
	THR(command_queue->Signal(fence, value_to_signal));
	return value_to_signal;
}

void wait_for_fence(ID3D12Fence* fence, u64 value_to_wait, HANDLE event)
{
	u64 completed_value = fence->GetCompletedValue();
	if (completed_value < value_to_wait)
	{
		THR(fence->SetEventOnCompletion(value_to_wait, event));
		WaitForSingleObject(event, INFINITE);
	}
}

[[nodiscard]]
u64 flush_and_increment(ID3D12CommandQueue* command_queue, ID3D12Fence* fence, u64 value, HANDLE event)
{
	u64 value_to_signal = signal_and_increment(command_queue, fence, value);
	wait_for_fence(fence, value_to_signal, event);
	return value_to_signal;
}

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
	
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	HWND win_handle = Win32::create_window(1280, 720, "DeRex12");
	auto&& [width, height] = Win32::get_window_client_dims(win_handle);
	
	Alloc_Arena global_memory
	{
		.max_size = GiB(2),
		.base = (byte*)VirtualAlloc(0, GiB(2), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
	};
	AlwaysAssert(global_memory.base && "Failed to allocate memory from Windows");
	
	Game_Input gameInputBuffer[2] = {};
	Game_Input* newInputs = &gameInputBuffer[0];
	Game_Input* oldInputs = &gameInputBuffer[1];
	
	// DXC preparation -> from official github
	IDxcCompiler3* dx_compiler = nullptr;
	IDxcUtils* dx_utils = nullptr;
	THR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dx_utils)));
	THR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dx_compiler)));
	
	IDxcIncludeHandler* dx_incl_handler;
	THR(dx_utils->CreateDefaultIncludeHandler(&dx_incl_handler));
	
	LPCWSTR pszArgs[] =
	   {
	   L"simple.hlsl",        // Optional shader source file name for error reporting and for PIX shader source view.
		 L"-E", L"main",        // Entry point.
		 L"-T", L"ps_6_6",      // Target.
		 L"-Zs",                // Enable debug information (slim format)
		 L"-D", L"MYDEFINE=1",  // A single define.
		 L"-Fo", L"simple.bin", // Optional. Stored in the pdb. 
		 L"-Fd", L"simple.pdb", // Name of the pdb. This must either be supplied or the autogenerated file name must be used.
		 L"-Qstrip_reflect",    // Strip reflection into a separate blob. 
		 };
	
	//	===============================================================================================================================
	//  			Start od Directx12 initalization -> This will get compressed/abstracted later!
	//  ===============================================================================================================================
	constexpr u8 count_backbuffers = 3;
	b32 is_initalized = false;
	b32 vsync = true;
		
	ID3D12Device2* device = nullptr;
	ID3D12DebugDevice2* debug_device = nullptr;
		
	IDXGISwapChain4* swapchain = nullptr;
	ID3D12Resource* render_targets[count_backbuffers]{};
	ID3D12DescriptorHeap* rtv_heap = nullptr;
		
	ID3D12CommandQueue* command_queue = nullptr;
	ID3D12GraphicsCommandList* command_list = nullptr;
	ID3D12CommandAllocator* command_allocators[count_backbuffers]{};

	u32 rtv_descriptor_size = 0;
	u32 current_backbuffer_i = 0;
		
	ID3D12Fence* fence = nullptr;
	u64 fence_value = 0;
	u64 fence_values[count_backbuffers]{};
	HANDLE fence_event = nullptr;
	
	D3D12MA::Allocator* dx_allocator = nullptr;
	
	// Startup initalization Direct3D12
	{
		auto&& [client_width, client_height] = Win32::get_window_client_dims(win_handle);
		
		IDXGIFactory4* factory = nullptr;
		u32 factory_flags = 0;
		auto d = defer([&] { RELEASE_SAFE(factory); });
		
		// Enable Debug Layer
		{
			#ifdef _DEBUG
			ID3D12Debug1* debug_controller;
			THR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)));
			debug_controller->EnableDebugLayer();
			debug_controller->SetEnableGPUBasedValidation(true);
			factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
			
			IDXGIInfoQueue* info_queue;
			THR(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&info_queue)));
			info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			
			debug_controller->Release();
			info_queue->Release();
			#endif
		}
		
		// Create Factory
		THR(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)));
		
		// Query for dedicated GPU, skip software & check support for D3D12
		IDXGIAdapter1* adapter = nullptr;
		{
			IDXGIFactory6* temp_factory = nullptr;
			THR(factory->QueryInterface(IID_PPV_ARGS(&temp_factory)));
			
			for (
				u32 i = 0;
				SUCCEEDED(temp_factory->EnumAdapterByGpuPreference(
					i,
					DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
					IID_PPV_ARGS(&adapter)));
				i++)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);
				
				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{ continue; }
				
				if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, _uuidof(ID3D12Device2), nullptr)))
				{ break; }
				
				adapter->Release();
			}
			temp_factory->Release();
			AlwaysAssert(adapter != nullptr && "Couldnt find dedicated GPU");
		}
			
		// Create Device
		THR(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device)));
		#ifdef _DEBUG
		THR(device->QueryInterface(&debug_device));
		#endif
		
		// Create D3D12 Allocator
		{
			D3D12MA::ALLOCATOR_DESC alloc_desc
			{
				.pDevice = device,
				.pAdapter = adapter
			};

			THR(D3D12MA::CreateAllocator(&alloc_desc, &dx_allocator));
		}
		
		// Create Command Queue
		{
			D3D12_COMMAND_QUEUE_DESC queue_desc 
			{
				.Type			= D3D12_COMMAND_LIST_TYPE_DIRECT,
				.Priority	= D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
				.Flags		= D3D12_COMMAND_QUEUE_FLAG_NONE, 
				.NodeMask	= 0 
			};
			THR(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));
		}
		
		// Create Swap chain
		{
			DXGI_SWAP_CHAIN_DESC1 swapchain_desc 
			{
				.Width 				= client_width,
				.Height 			= client_height,
				.Format 			= DXGI_FORMAT_R8G8B8A8_UNORM,
				.Stereo				= false,
				.SampleDesc		= {1, 0},
				.BufferUsage	= DXGI_USAGE_RENDER_TARGET_OUTPUT,
				.BufferCount	= 3,
				.Scaling			= DXGI_SCALING_NONE,
				.SwapEffect		= DXGI_SWAP_EFFECT_FLIP_DISCARD,
				.AlphaMode		= DXGI_ALPHA_MODE_UNSPECIFIED,
				.Flags				= 0 //TODO: G-sync support
			};
			IDXGISwapChain1* temp_swapchain = nullptr;
			THR(factory->CreateSwapChainForHwnd(command_queue, win_handle, &swapchain_desc, 0, 0, &temp_swapchain));
			THR(temp_swapchain->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&swapchain));
			current_backbuffer_i = swapchain->GetCurrentBackBufferIndex();
			
			temp_swapchain->Release();
		}
		
		// Create Descriptor heap for RTVs
		{
			D3D12_DESCRIPTOR_HEAP_DESC rtv_desc
			{
				.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
				.NumDescriptors	= count_backbuffers,
				.Flags					= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
				.NodeMask				= 0
			};
			THR(device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)));
			rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		}
		
		// Create fence
		THR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		fence_event = CreateEvent(0, false, false, 0);
		AlwaysAssert(fence_event && "Failed creation of fence event");
		
		// Create Command Allocator
		for (u32 i = 0; i < count_backbuffers; i++)
		{
			THR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
			                                               IID_PPV_ARGS(&command_allocators[i])));
		}
		
		// Create Command Lists
		THR(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		                                          command_allocators[current_backbuffer_i], nullptr,
		                                          IID_PPV_ARGS(&command_list)));
		THR(command_list->Close()); 
	}
	
	//  ===============================================================================================================================	
	
	// Basic D3D12 needed for actual rendering
	ID3D12RootSignature* root_sig = nullptr;
	ID3D12PipelineState* pso = nullptr;
	ID3D12Resource* vertex_buffer = nullptr; 
	CD3DX12_VIEWPORT viewport{};
	CD3DX12_RECT scissor_rect{};
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
	
	// Pipeline creation
	{
		// Create root signature TODO: Take signature directly from compiled shader
		{
			D3D12_VERSIONED_ROOT_SIGNATURE_DESC  root_sig_desc;
			root_sig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
			D3D12_ROOT_SIGNATURE_DESC2 desc2
			{
				.NumParameters = 0,
				.pParameters = nullptr,
				.NumStaticSamplers = 0,
				.pStaticSamplers = nullptr,
				.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT //TODO: with bindless, will be changed,
				         // to pull vertex approach without IA calls
			};
			root_sig_desc.Desc_1_2 = desc2;
		
			ID3DBlob* signature = nullptr;
			ID3DBlob* error = nullptr;
			auto d = defer([&] { RELEASE_SAFE(signature); RELEASE_SAFE(error); });
		
			THR(D3D12SerializeVersionedRootSignature(&root_sig_desc, &signature, &error));
			THR(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
			root_sig->SetName(L"Basic root signature");
		}
	
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC ia_layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
	}
	
	while (Win32::g_is_running)
	{
		u64 tick_start = Win32::get_performance_ticks();
		static u32 counter;
		
		// Input handling
		Game_Controller* oldKeyboardMouseController = get_game_controller(oldInputs, 0);
		Game_Controller* newKeyboardMouseController = get_game_controller(newInputs, 0);
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
		if (new_width != width || new_height != height || !render_targets[0])
		{
			width	= new_width;
			height	= new_height;
			
			fence_value = flush_and_increment(command_queue, fence, fence_value, fence_event);
			
			if (render_targets[0])
			{
				for (u32 i = 0; i < count_backbuffers; i++)
				{
					render_targets[i]->Release();
					render_targets[i] = nullptr;
					fence_values[i] = fence_values[current_backbuffer_i];
				}
			}
			
			if (width && height)
			{
				// Resize swap chain
				DXGI_SWAP_CHAIN_DESC swapchain_desc{};
				THR(swapchain->GetDesc(&swapchain_desc));
				THR(swapchain->ResizeBuffers(count_backbuffers, width, height, 
				                                         swapchain_desc.BufferDesc.Format, swapchain_desc.Flags));
			
				current_backbuffer_i = swapchain->GetCurrentBackBufferIndex();
			
				// Create/Update RTVs
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
				for (u32 i = 0; i < count_backbuffers; i++)
				{
					THR(swapchain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i])));
					device->CreateRenderTargetView(render_targets[i], nullptr, rtv_handle);
					rtv_handle.Offset(1, rtv_descriptor_size);
				}
				
				// Update sizes of scissor, viewport
				viewport	= CD3DX12_VIEWPORT( 0.0f, 0.0f, (float)width, (float)height );
				scissor_rect = CD3DX12_RECT	( 0, 			0, 	(u32)width, 	(u32)height );
			}
		}
		
		// D3D12 render
		if (width && height)
		{
			auto command_allocator = command_allocators[current_backbuffer_i];
			auto backbuffer = render_targets[current_backbuffer_i];
			
			command_allocator->Reset();
			// Using one command list with 3 allocators (one for each backbuffer)
			command_list->Reset(command_allocator, nullptr);
			
			// Clear render target
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
				                                                                        D3D12_RESOURCE_STATE_PRESENT,
				                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
				command_list->ResourceBarrier(1, &barrier);
				lib::Vec4 color { 0.42f, 0.14f, 0.3f, 1.0f };
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtv_heap->GetCPUDescriptorHandleForHeapStart(),
				                                  current_backbuffer_i, rtv_descriptor_size);
				command_list->ClearRenderTargetView(rtv, color.e, 0, nullptr);
			}
			
			// Present
			{
				CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
				                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET,
				                                                                        D3D12_RESOURCE_STATE_PRESENT);
				command_list->ResourceBarrier(1, &barrier);
				THR(command_list->Close());
				
				ID3D12CommandList* const commandLists[] = { command_list };
				command_queue->ExecuteCommandLists(_countof(commandLists), commandLists);
				
				// Signal - temporary values are needed when signaling to avoid race condition, even when single CPU thread is used
				fence_values[current_backbuffer_i] = fence_value = signal_and_increment(command_queue, fence, fence_value);
				
				// Present current backbuffer
				THR(swapchain->Present(1, 0));
				
				// Get next backbuffer - updated on swapchain by Present()
				current_backbuffer_i = swapchain->GetCurrentBackBufferIndex();
				
				// Wait for frame (n-1), so we are not waiting on 'this' frame
				wait_for_fence(fence, fence_values[current_backbuffer_i], fence_event);
			}
		}
		
		f64 frame_time_ms = Win32::get_elapsed_ms_here(clock, tick_start);
		
		if (counter % 100 == 0)
		{
			char time_buf[32];
			sprintf_s(time_buf, sizeof(time_buf), "%.3lf ms", frame_time_ms);
			SetWindowText(win_handle, time_buf); //! WARNING - this might stall with very high fps (0.001ms)!
		}
	}
	//TODO: Rest of Cleanup
	RELEASE_SAFE(dx_allocator);
	debug_device->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
	UnregisterClassA("DeRex12", GetModuleHandle(nullptr));
	return 0;
}