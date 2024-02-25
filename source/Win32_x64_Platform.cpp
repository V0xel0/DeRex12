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

#ifdef _DEBUG
inline void THR(HRESULT hr) {
	AlwaysAssert(SUCCEEDED(hr));
}
#else
inline void THR(HRESULT) {}
#endif

#define RELEASE_SAFE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

#include "DX_Managment.hpp"
#include "DX_Managment.cpp"
#include "Renderer.hpp"

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

	Game_Window game_window { (void*)win_handle, width, height };
	
	Game_Input game_input_buffer[2] = {};
	Game_Input* new_inputs = &game_input_buffer[0];
	Game_Input* old_inputs = &game_input_buffer[1];
	
	renderer_update_ptr *update_renderer = renderer_full_update;
	
	//	===============================================================================================================================
	//  			Start od Directx12 initalization -> This will get compressed/abstracted later!
	//  ===============================================================================================================================
	constexpr u8 count_backbuffers = 3;
	b32 is_initalized = false;
	b32 vsync = true;
		
	ID3D12Device2* device = nullptr;
	ID3D12DebugDevice2* debug_device = nullptr;
		
	IDXGISwapChain4* swapchain = nullptr;
	ID3D12Resource* rtv_texture[count_backbuffers]{};
	ID3D12DescriptorHeap* rtv_heap = nullptr;
	
	GPU_Resource dsv_texture{};
	ID3D12DescriptorHeap* dsv_heap = nullptr;
		
	ID3D12CommandQueue* queue_direct = nullptr;
	ID3D12GraphicsCommandList* cmd_list_direct = nullptr;
	ID3D12CommandAllocator* cmd_allocators_direct[count_backbuffers]{};

	u32 rtv_descriptor_size = 0;
	u32 back_buffer_i = 0;
		
	ID3D12Fence* fence_direct = nullptr;
	u64 fence_counter = 0;
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
			THR(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_direct)));
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
			THR(factory->CreateSwapChainForHwnd(queue_direct, win_handle, &swapchain_desc, 0, 0, &temp_swapchain));
			THR(temp_swapchain->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&swapchain));
			back_buffer_i = swapchain->GetCurrentBackBufferIndex();
			
			temp_swapchain->Release();
		}
		
		// Create RTV heap
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
		
		// Create DSV heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC dsv_desc
			{
				.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
				.NumDescriptors	= 1,
				.Flags					= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
				.NodeMask				= 0
			};
			THR(device->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap)));
		}
		
		// Create fence
		THR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_direct)));
		fence_event = CreateEvent(0, false, false, 0);
		AlwaysAssert(fence_event && "Failed creation of fence event");
		
		// Create Command Allocator
		for (u32 i = 0; i < count_backbuffers; i++)
		{
			THR(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
			                                               IID_PPV_ARGS(&cmd_allocators_direct[i])));
		}
		
		// Create Command Lists
		THR(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		                                          cmd_allocators_direct[back_buffer_i], nullptr,
		                                          IID_PPV_ARGS(&cmd_list_direct)));
		THR(cmd_list_direct->Close()); 
	}
	
	//  ===============================================================================================================================	
	
	// Basic D3D12 needed for actual rendering
	ID3D12RootSignature* root_sig = nullptr;
	ID3D12PipelineState* pso = nullptr;
	
	GPU_Resource vertices_static{};
	GPU_Resource indices_static{};
	
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
		
			THR(D3D12SerializeVersionedRootSignature(&root_sig_desc, &signature, &error)); // Serialized 'signature' could be cached if needed
			THR(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
			root_sig->SetName(L"Basic root signature");
		}
	
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC ia_layout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		
		// Compile shaders
		// TODO: More code compression, caching for results, save pdb's and reflection, compile if new hash
		LPCWSTR path_shader = L"../source/shaders/simple.hlsl";
		auto result_ps = compile_shader_default(path_shader, L"simplePS", L"PSMain", L"ps_6_6");
		auto result_vs = compile_shader_default(path_shader, L"simpleVS", L"VSMain", L"vs_6_6");
		IDxcBlob* pixel_shader;
		THR(result_ps->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pixel_shader), nullptr));
		IDxcBlob* vertex_shader;
		THR(result_vs->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&vertex_shader), nullptr));
		
		// Create default PSO
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
			pso_desc.InputLayout = { ia_layout, _countof(ia_layout) };
			pso_desc.pRootSignature = root_sig;
			pso_desc.VS = { vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize() };
			pso_desc.PS = { pixel_shader->GetBufferPointer(),  pixel_shader->GetBufferSize() };
			pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			pso_desc.SampleMask = UINT_MAX;
			pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso_desc.NumRenderTargets = 1;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			pso_desc.SampleDesc.Count = 1;
			
			THR(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)));
		}
	}
	
	// Static data initalization
	{
		Array_View<Vertex>vertex_data{};
		vertex_data.init(&platform_arena, 
		                 Vertex{ {	-0.5f, 0.5f, 0.5f, 1.0f },	{ 1.0f, 0.0f, 0.0f, 1.0f } },
										 Vertex{ {	0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 1.0f, 0.0f, 1.0f } },
										 Vertex{ { -0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.5f,  0.5f, 0.5f, 1.0f }, 	{ 0.0f, 1.0f, 1.0f, 1.0f } },
		
										 Vertex{ { -0.75f, 0.75f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,	 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ { -0.75f, 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,  0.75f,	0.7f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		
		Array_View<u16>indices_data{};
		indices_data.init(&platform_arena, 0, 1, 2,  0, 3, 1);
		
		auto command_allocator = cmd_allocators_direct[back_buffer_i];
		THR(cmd_list_direct->Reset(command_allocator, nullptr));
		
		// Create heaps and upload data
		vertices_static = upload_static_data(vertex_data, cmd_list_direct, device);
		indices_static = upload_static_data(indices_data, cmd_list_direct, device);
		                 
		// executing all uploads 
		{
			THR(cmd_list_direct->Close());
			ID3D12CommandList* const commandLists[] = { cmd_list_direct };
			queue_direct->ExecuteCommandLists(_countof(commandLists), commandLists);
			// TODO: For now, lets fully stall on CPU until GPU finished copying all data, async this in future
			fence_counter = flush(queue_direct, fence_direct, fence_counter, fence_event);
		}
	}
	
	while (Win32::g_is_running)
	{
		u64 tick_start = Win32::get_performance_ticks();
		static u32 counter;
		
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
		
		// Update renderer
		game_window.width = new_width;
		game_window.height = new_height;
		update_renderer(&game_memory, &game_window, new_inputs);
		
		if (new_width != width || new_height != height || !rtv_texture[0])
		{
			width	= new_width;
			height	= new_height;
			
			fence_counter = flush(queue_direct, fence_direct, fence_counter, fence_event);
			
			if (rtv_texture[0])
			{
				for (u32 i = 0; i < count_backbuffers; i++)
				{
					rtv_texture[i]->Release();
					rtv_texture[i] = nullptr;
					fence_values[i] = fence_values[back_buffer_i];
				}
			}
			
			if (width && height)
			{
				// Resize swap chain
				DXGI_SWAP_CHAIN_DESC swapchain_desc{};
				THR(swapchain->GetDesc(&swapchain_desc));
				THR(swapchain->ResizeBuffers(count_backbuffers, width, height, 
				                                         swapchain_desc.BufferDesc.Format, swapchain_desc.Flags));
			
				back_buffer_i = swapchain->GetCurrentBackBufferIndex();
			
				// Create/Update RTV textures
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
				for (u32 i = 0; i < count_backbuffers; i++)
				{
					THR(swapchain->GetBuffer(i, IID_PPV_ARGS(&rtv_texture[i])));
					device->CreateRenderTargetView(rtv_texture[i], nullptr, rtv_handle);
					rtv_handle.Offset(1, rtv_descriptor_size);
				}
				
				// Create/Update DSV, depth texture
				{
					dsv_texture.desc = CD3DX12_RESOURCE_DESC::Tex2D
							(DXGI_FORMAT_D32_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

					THR(device->CreateCommittedResource(
						get_const_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
						D3D12_HEAP_FLAG_NONE,
						&dsv_texture.desc,
						D3D12_RESOURCE_STATE_DEPTH_WRITE,
						get_const_ptr(CD3DX12_CLEAR_VALUE(dsv_texture.desc.Format, 1.0f, 0)),
						IID_PPV_ARGS(&dsv_texture.ptr)
					));
					dsv_heap->SetName(L"Depth/Stencil Resource Heap");
					
					// Create/Update descriptor/view
					{
						D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc 
						{
							.Format 						=	DXGI_FORMAT_D32_FLOAT,
							.ViewDimension 			= D3D12_DSV_DIMENSION_TEXTURE2D,
							.Flags 							= D3D12_DSV_FLAG_NONE,
							.Texture2D {.MipSlice = 0}
						};
						device->CreateDepthStencilView(dsv_texture.ptr, &dsv_desc, 
						                               dsv_heap->GetCPUDescriptorHandleForHeapStart());
					}
				}
			}
		}
			
		// D3D12 render
		if (width && height)
		{
			auto cmd_alloc = cmd_allocators_direct[back_buffer_i];
			auto backbuffer = rtv_texture[back_buffer_i];
			
			THR(cmd_alloc->Reset());
			// Reset current command list taken from current command allocator
			THR(cmd_list_direct->Reset(cmd_alloc, pso));
			
			// Clear rtv & dsv
			{
				cmd_list_direct->ResourceBarrier(1, get_const_ptr(CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
																																				D3D12_RESOURCE_STATE_PRESENT,
																																				D3D12_RESOURCE_STATE_RENDER_TARGET)));
				lib::Vec4 color { 0.42f, 0.14f, 0.3f, 1.0f };
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart(),
				                                  				back_buffer_i, rtv_descriptor_size);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap->GetCPUDescriptorHandleForHeapStart());
				
				cmd_list_direct->ClearRenderTargetView(rtv_handle, color.e, 0, nullptr);
				cmd_list_direct->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				cmd_list_direct->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
			}
			
			// Populate command list - drawing with default state
			{
				// Default state
				cmd_list_direct->RSSetViewports(1, get_const_ptr(CD3DX12_VIEWPORT(0.0f, 0.0f, (f32)width, (f32)height)));
				cmd_list_direct->RSSetScissorRects(1, get_const_ptr(CD3DX12_RECT(0, 0, (u32)width, (u32)height)));
				cmd_list_direct->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				
				// Drawing
				//command_list->SetPipelineState(pso);
				cmd_list_direct->SetGraphicsRootSignature(root_sig);
				D3D12_VERTEX_BUFFER_VIEW vb_view_static 
				{ 
					.BufferLocation = vertices_static.ptr->GetGPUVirtualAddress(),
					.SizeInBytes = (UINT)vertices_static.desc.Width,
					.StrideInBytes = sizeof(Vertex)
				};
				cmd_list_direct->IASetVertexBuffers(0, 1, &vb_view_static);
				D3D12_INDEX_BUFFER_VIEW ib_view_static
				{
					.BufferLocation = indices_static.ptr->GetGPUVirtualAddress(),
					.SizeInBytes = (UINT)indices_static.desc.Width,
					.Format = DXGI_FORMAT_R16_UINT
				};
				
				cmd_list_direct->IASetIndexBuffer(&ib_view_static);
				cmd_list_direct->DrawIndexedInstanced(6, 1, 0, 0, 0);
				cmd_list_direct->DrawIndexedInstanced(6, 1, 0, 4, 0);
			}
			
			// Present
			{
				cmd_list_direct->ResourceBarrier(1, get_const_ptr
				                          (CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
																																				D3D12_RESOURCE_STATE_RENDER_TARGET,
																																				D3D12_RESOURCE_STATE_PRESENT)));
				THR(cmd_list_direct->Close());
				
				ID3D12CommandList* const commandLists[] = { cmd_list_direct };
				queue_direct->ExecuteCommandLists(_countof(commandLists), commandLists);
				
				// Present current backbuffer
				THR(swapchain->Present(1, 0));
				
				// Signal - through fence_counter when signaling to avoid race condition
				fence_values[back_buffer_i] = fence_counter = signal(queue_direct, fence_direct, fence_counter);
				
				// Get next backbuffer - updated on swapchain by Present()
				back_buffer_i = swapchain->GetCurrentBackBufferIndex();
				
				// Wait for frame (n-1), so we are not waiting on 'this' frame
				wait_for_fence(fence_direct, fence_values[back_buffer_i], fence_event);
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