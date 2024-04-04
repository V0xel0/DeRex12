#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Views.hpp"
#include "Math.hpp"

#include "RHI_D3D12.hpp"
#include <dxcapi.h>       
#include <d3d12shader.h>

#include "Render_Data.hpp"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 611;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = "..\\external\\D3D12\\"; }

namespace DX
{
	internal constexpr u32 g_alloc_alignment = 256;
	internal constexpr u64 g_upload_heap_max_size = MiB(20);
	
	[[nodiscard]]
	internal u64 signal(Context* ctx)
	{
		u64 value_to_signal = ++ctx->fence.fence_counter;
		THR(ctx->queue->Signal(ctx->fence.ptr, value_to_signal));
		return value_to_signal;
	}

	internal void sync_with_fence(DX::Fence* fence, u64 value_to_wait)
	{
		u64 completed_value = fence->ptr->GetCompletedValue();
		if (completed_value < value_to_wait)
		{
			THR(fence->ptr->SetEventOnCompletion(value_to_wait, fence->fence_event));
			WaitForSingleObject(fence->fence_event, INFINITE);
		}
	}

	[[nodiscard]]
	internal void wait_for_work(Context* ctx)
	{
		u64 value_to_signal = signal(ctx);
		sync_with_fence(&ctx->fence, value_to_signal);
	}
	
	extern void execute_and_wait(Context* ctx)
	{
		THR(ctx->cmd_list->Close());
		ID3D12CommandList* const commandLists[] = { ctx->cmd_list };
		ctx->queue->ExecuteCommandLists(_countof(commandLists), commandLists);
		wait_for_work(ctx);
	}
	
	[[nodiscard]]
	internal IDxcResult* compile_shader_default(LPCWSTR path, LPCWSTR name, LPCWSTR entry_point, LPCWSTR target)
	{
		LPCWSTR args[] =
		{
		name,      // Optional shader source file name for error reporting and for PIX shader source view.
		L"-E", entry_point,        // Entry point.
		L"-T", target,      // Target.
		L"-Zs",                // Enable debug information (slim format)
		L"-D", L"MYDEFINE=1",  // A single define.
		L"-Fo", name, // Optional. Stored in the pdb. 
		L"-Fd", name, // Name of the pdb. This must either be supplied or the autogenerated file name must be used.
		L"-Qstrip_reflect",    // Strip reflection into a separate blob. 
		L"-Zpc",
		};
		
		//TODO: Consider reuse for same thread
		IDxcCompiler3* dx_compiler = nullptr;
		IDxcUtils* dx_utils = nullptr;
		IDxcIncludeHandler* dx_incl_handler = nullptr;
		IDxcResult* result = nullptr;
		IDxcBlobUtf8* error = nullptr;
		 
		// Defer without IDxcResult
		auto d = defer([&] { RELEASE_SAFE(dx_compiler); RELEASE_SAFE(dx_utils); RELEASE_SAFE(dx_incl_handler);
		RELEASE_SAFE(error); });
			 
		THR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dx_utils)));
		THR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dx_compiler)));
		THR(dx_utils->CreateDefaultIncludeHandler(&dx_incl_handler));
			 
		IDxcBlobEncoding* ptr_source = nullptr;
		THR(dx_utils->LoadFile(path, nullptr, &ptr_source));
		DxcBuffer source;
		source.Ptr = ptr_source->GetBufferPointer();
		source.Size = ptr_source->GetBufferSize();
		source.Encoding = DXC_CP_ACP;
			
		// Compile
		dx_compiler->Compile(
			&source,              // Source buffer.
			args,                // Array of pointers to arguments.
			_countof(args),      // Number of arguments.
			dx_incl_handler,     // User-provided interface to handle #include directives (optional).
			IID_PPV_ARGS(&result) // Compiler output status, buffer, and errors.
		);
			 
		// Print errors & warnings
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error), nullptr);
		if (error != nullptr && error->GetStringLength() != 0)
		{ OutputDebugStringA(error->GetStringPointer()); }
			
		// Check compilation status
		HRESULT hr_status;
		result->GetStatus(&hr_status);
		if (FAILED(hr_status))
		{ wprintf(L"Compilation Failed\n"); }
			
		return result;
	}
	
	[[nodiscard]]
	internal Memory_Heap create_memory_heap(Device* dev, u64 max_bytes, D3D12_HEAP_TYPE type)
	{
		auto* device = dev->ptr;
		Memory_Heap out{};
		
		THR(device->CreateCommittedResource(get_const_ptr<D3D12_HEAP_PROPERTIES>({ .Type = type }),
		                                    D3D12_HEAP_FLAG_NONE,
		                                    get_const_ptr(CD3DX12_RESOURCE_DESC::Buffer(max_bytes)),
		                                    D3D12_RESOURCE_STATE_GENERIC_READ,
		                                    nullptr,
		                                    IID_PPV_ARGS(&out.heap)));
		
		THR(out.heap->Map(0, 
		                  get_const_ptr<D3D12_RANGE>({ .Begin = 0, .End = 0 }), 
		                  (void**) &out.heap_arena.base));
		
		out.gpu_base = out.heap->GetGPUVirtualAddress();
		out.heap_arena.max_size = max_bytes;
		
		return out;
	}
	
	[[nodiscard]]
	internal auto allocate_gpu_memory(Memory_Heap* heap, u64 size)
	{
		struct { u8* addr_cpu; D3D12_GPU_VIRTUAL_ADDRESS addr_gpu; }out;
		out.addr_cpu = (u8*)allocate(&heap->heap_arena, size, g_alloc_alignment);
		out.addr_gpu = heap->gpu_base + heap->heap_arena.prev_offset; //prev offset was curr offset in cpu alloc
		return out;
	}
	
	internal void reset_gpu_memory(Memory_Heap* heap)
	{
		arena_reset(&heap->heap_arena);
	}
	
	extern void rhi_init(RHI_State* state, void* win_handle, u32 client_width, u32 client_height)
	{
		auto* ctx = &state->ctx_direct;
		
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
		THR(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&state->device.ptr)));
		#ifdef _DEBUG
		THR(state->device.ptr->QueryInterface(&state->device.d_ptr));
		#endif
			
		// Create Command Queue
		{
			D3D12_COMMAND_QUEUE_DESC queue_desc 
			{
				.Type			= D3D12_COMMAND_LIST_TYPE_DIRECT,
				.Priority	= D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
				.Flags		= D3D12_COMMAND_QUEUE_FLAG_NONE, 
				.NodeMask	= 0 
			};
			THR(state->device.ptr->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&ctx->queue)));
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
			THR(factory->CreateSwapChainForHwnd(ctx->queue, (HWND)win_handle, &swapchain_desc, 0, 0, &temp_swapchain));
			THR(temp_swapchain->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&state->swapchain));
			state->frame_index = state->swapchain->GetCurrentBackBufferIndex();
			
			temp_swapchain->Release();
		}
		
		// Create RTV heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC rtv_desc
			{
				.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
				.NumDescriptors	= g_count_backbuffers,
				.Flags					= D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
				.NodeMask				= 0
			};
			THR(state->device.ptr->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&state->rtv_heap)));
			state->rtv_descriptor_size = state->device.ptr->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
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
			THR(state->device.ptr->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&state->dsv_heap)));
		}
		
		// Create heaps
		{
			for(u32 frame_i = 0; frame_i < g_count_backbuffers; ++frame_i)
			{
				state->upload_heaps[frame_i] = create_memory_heap(&state->device, g_upload_heap_max_size, D3D12_HEAP_TYPE_UPLOAD);
			}
		}
		
		// Create fence
		THR(state->device.ptr->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx->fence.ptr)));
		ctx->fence.fence_event = CreateEvent(0, false, false, 0);
		AlwaysAssert(ctx->fence.fence_event && "Failed creation of fence event");
		
		// Create Command Allocators
		for (u32 i = 0; i < g_count_backbuffers; i++)
		{
			THR(state->device.ptr->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
			                                   IID_PPV_ARGS(&ctx->cmd_allocators[i])));
			THR(ctx->cmd_allocators[i]->Reset());
		}
		
		// Create Initial Command List
		THR(state->device.ptr->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		                              ctx->cmd_allocators[state->frame_index], nullptr,
		                              IID_PPV_ARGS(&ctx->cmd_list)));
		THR(ctx->cmd_list->Close());
		THR(ctx->cmd_list->Reset(ctx->cmd_allocators[state->frame_index], nullptr));
		
		state->is_initalized = true;
	}
	
	[[nodiscard]]
	extern Pipeline create_basic_pipeline(Device* dev, const wchar_t* vs_ps_path)
	{
		auto* device = dev->ptr;
		Pipeline out{};
		
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC ia_layout[] =
		{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};
		
		// Compile shaders
		auto result_ps = compile_shader_default(vs_ps_path, L"simplePS", L"PSMain", L"ps_6_6");
		auto result_vs = compile_shader_default(vs_ps_path, L"simpleVS", L"VSMain", L"vs_6_6");
		IDxcBlob* pixel_shader;
		THR(result_ps->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pixel_shader), nullptr));
		IDxcBlob* vertex_shader;
		THR(result_vs->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&vertex_shader), nullptr));
		
		// Create root signature from shader
		THR(device->CreateRootSignature(0, vertex_shader->GetBufferPointer(), 
			                            	vertex_shader->GetBufferSize(), IID_PPV_ARGS(&out.root_signature)));
		
		// Create default PSO
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
			pso_desc.InputLayout = { ia_layout, _countof(ia_layout) };
			pso_desc.pRootSignature = out.root_signature;
			pso_desc.VS = { vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize() };
			pso_desc.PS = { pixel_shader->GetBufferPointer(),  pixel_shader->GetBufferSize() };
			pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			pso_desc.SampleMask = UINT_MAX;
			pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pso_desc.NumRenderTargets = 1;
			pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT; // Sync this!
			pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // Sync this!
			pso_desc.SampleDesc.Count = 1;
			// For RH coordinate system, CCW winding order is needed
			pso_desc.RasterizerState.FrontCounterClockwise = TRUE;
			
			THR(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&out.pso)));
		}
		return out;
	}
	
	extern void render_frame(RHI_State* state, Data_To_RHI* data_from_app, u32 new_width, u32 new_height)
	{
		// TEMPORARY helper autos
		auto* ctx 		= &state->ctx_direct; // for now only direct context
		auto* device 	= state->device.ptr;
		
		auto& width 	= state->width;
		auto& height 	= state->height;
		
		auto* swapchain 					= state->swapchain;
		auto* rtv_heap 						= state->rtv_heap;
		auto* rtv_texture 				= state->rtv_texture;
		auto& rtv_descriptor_size	= state->rtv_descriptor_size;
		auto* dsv_heap 						= state->dsv_heap;
		auto& dsv_texture 				= state->dsv_texture;
		
		auto& fence_values 	= state->fence_values;
		auto& frame_index 	= state->frame_index;
		
		auto& vertices_static 	= data_from_app->verts;
		auto& indices_static 		= data_from_app->indices;
		auto& pipeline_default 	= data_from_app->default_pipeline;
		
		// RTV & DSV check for re/creation
		if (new_width != width || new_height != height || !rtv_texture[0])
		{
			width	= new_width;
			height = new_height;
			
			wait_for_work(ctx);
			
			if (rtv_texture[0])
			{
				for (u32 i = 0; i < g_count_backbuffers; i++)
				{
					rtv_texture[i]->Release();
					rtv_texture[i] = nullptr;
					fence_values[i] = fence_values[frame_index];
				}
			}
			
			if (width && height)
			{
				// Resize swap chain
				DXGI_SWAP_CHAIN_DESC swapchain_desc{};
				THR(swapchain->GetDesc(&swapchain_desc));
				THR(swapchain->ResizeBuffers(g_count_backbuffers, width, height, 
				                             swapchain_desc.BufferDesc.Format, swapchain_desc.Flags));
			
				frame_index = swapchain->GetCurrentBackBufferIndex();
			
				// Create/Update RTV textures
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart());
				for (u32 i = 0; i < g_count_backbuffers; i++)
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
			auto cmd_alloc = ctx->cmd_allocators[frame_index];
			auto backbuffer = rtv_texture[frame_index];
			auto* upload_heap = &state->upload_heaps[frame_index];
			
			THR(cmd_alloc->Reset());
			// Reset current command list taken from current command allocator
			THR(ctx->cmd_list->Reset(cmd_alloc, pipeline_default.pso));
			
			// Clear rtv & dsv
			{
				ctx->cmd_list->ResourceBarrier(1, get_const_ptr(CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
				                                                                                    D3D12_RESOURCE_STATE_PRESENT,
				                                                                                    D3D12_RESOURCE_STATE_RENDER_TARGET)));
				lib::Vec4 color { 0.42f, 0.14f, 0.3f, 1.0f };
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->GetCPUDescriptorHandleForHeapStart(),
				                                         frame_index, rtv_descriptor_size);
				CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(dsv_heap->GetCPUDescriptorHandleForHeapStart());
				
				ctx->cmd_list->ClearRenderTargetView(rtv_handle, color.e, 0, nullptr);
				ctx->cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				ctx->cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);
			}
			
			// Push per frame constants
			D3D12_GPU_VIRTUAL_ADDRESS cbv_gpu_addr_frame{};
			{
				const auto [cpu_addr, gpu_addr] = allocate_gpu_memory(upload_heap, sizeof(Constant_Data_Frame));
				cbv_gpu_addr_frame = gpu_addr;
				Constant_Data_Frame* frame_consts = (Constant_Data_Frame*)cpu_addr;
				// Copy to gpu upload heap
				*frame_consts = data_from_app->cb_frame;
			}
			
			// Push per draw constants
			D3D12_GPU_VIRTUAL_ADDRESS cbv_gpu_addr_draw{};
			{
				const auto [cpu_addr, gpu_addr] = allocate_gpu_memory(upload_heap, sizeof(Constant_Data_Draw));
				cbv_gpu_addr_draw = gpu_addr;
				Constant_Data_Draw* draw_consts = (Constant_Data_Draw*)cpu_addr;
				// Copy to gpu upload heap
				*draw_consts = data_from_app->cb_draw;
			}
			
			// Populate command list
			{
				// Default state
				ctx->cmd_list->RSSetViewports(1, get_const_ptr(CD3DX12_VIEWPORT(0.0f, 0.0f, (f32)width, (f32)height)));
				ctx->cmd_list->RSSetScissorRects(1, get_const_ptr(CD3DX12_RECT(0, 0, (u32)width, (u32)height)));
				ctx->cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				
				// Drawing static data
				{
					//command_list->SetPipelineState(pso);
					ctx->cmd_list->SetGraphicsRootSignature(pipeline_default.root_signature);
					ctx->cmd_list->SetGraphicsRootConstantBufferView(1, cbv_gpu_addr_frame);
					ctx->cmd_list->SetGraphicsRootConstantBufferView(0, cbv_gpu_addr_draw);
					D3D12_VERTEX_BUFFER_VIEW vb_view_static 
					{ 
						.BufferLocation = vertices_static.ptr->GetGPUVirtualAddress(),
						.SizeInBytes = (UINT)vertices_static.desc.Width,
						.StrideInBytes = sizeof(Vertex)
					};
					ctx->cmd_list->IASetVertexBuffers(0, 1, &vb_view_static);
					D3D12_INDEX_BUFFER_VIEW ib_view_static
					{
						.BufferLocation = indices_static.ptr->GetGPUVirtualAddress(),
						.SizeInBytes = (UINT)indices_static.desc.Width,
						.Format = DXGI_FORMAT_R16_UINT
					};
				
					ctx->cmd_list->IASetIndexBuffer(&ib_view_static);
					ctx->cmd_list->DrawIndexedInstanced(36, 1, 0, 0, 0);
					//ctx->cmd_list->DrawIndexedInstanced(6, 1, 0, 4, 0);
				}
			}
			
			// Present
			{
				ctx->cmd_list->ResourceBarrier(1, get_const_ptr
				                             (CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
				                                                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
				                                                                   D3D12_RESOURCE_STATE_PRESENT)));
				THR(ctx->cmd_list->Close());
				
				ID3D12CommandList* const commandLists[] = { ctx->cmd_list };
				ctx->queue->ExecuteCommandLists(_countof(commandLists), commandLists);
				
				// Present current backbuffer
				THR(swapchain->Present(1, 0));
				
				// Signal the end of direct context work for this frame and store its value for later synchronization
				fence_values[frame_index] = signal(ctx);
				
				// Get next backbuffer index - updated on swapchain by Present()
				frame_index = swapchain->GetCurrentBackBufferIndex();
				
				// Wait for fence value from frame (n-1) - not waiting on current frame
				sync_with_fence(&ctx->fence, fence_values[frame_index]);
				// Upload heap from frame (n-1) is safe to reset cause work from that frame has finished
				reset_gpu_memory(&state->upload_heaps[frame_index]);
			}
		}
		
	}
	
} // namespace DX