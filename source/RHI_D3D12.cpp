/* TODO
* 
*	 2) Heaps + descriptors creation & abstraction
*	 3) Pools + handles
*	 4) Data manager for GPU/CPU renderable resources
*	 6) Shader abstraction
*	 7) rhi_run() split
*  8) Shader compilation improvements: reflection, signature from shader, output debug
*  9) Shader creation code compression, caching for results, save pdb's and reflection, recompile if new hash
* 10) "Pipeline" struct may be changed to binary relation 1..n of '1' root signature to 'n' PSOs
* 				This could be done as 2D array instead of generic solution like: https://github.com/RonPieket/BinaryRelations
* 11) Maybe also reset gpu_memory in "execute_and_wait"?
* 12) upload_static_data has to be changed to be a data thats const per "level" and needs upload 
* 			to default heap. In data sent from app to rhi then we would have 2 streams of data depending
* 			on frequency -> or streams of render passses later
* 14) RHI_State seperation for logical & data, also caching system for it
* 15) create_basic_pipeline has to be split from shader creation and maybe generalized to any pipeline 
* 16) maybe keep descriptors tied to DescriptorHeap (by additional id)?
* */

#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Views.hpp"
#include "Math.hpp"

#define NOMINMAX

#include "RHI_D3D12.hpp"
#include "../external/dxc/dxcapi.h"       
#include "../external/dxc/d3d12shader.h"

#include "Render_Data.hpp"

#include "DDSTextureLoader12.cpp"

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

namespace DX
{
	internal constexpr u32 g_alloc_alignment = 512;
	internal constexpr u64 g_upload_heap_max_size = MiB(500);
	internal constexpr u64 g_max_count_cbv_srv_uav_descriptors = 128;
	internal constexpr u32 g_max_count_texture_subresource = 72; // max: 12mips for cubemap
	
	internal RHI_State g_state{};
		
	[[nodiscard]]
	internal u64 signal(Context* ctx)
	{
		u64 value_to_signal = ++ctx->fence.counter;
		THR(ctx->queue->Signal(ctx->fence.ptr, value_to_signal));
		return value_to_signal;
	}

	internal void sync_with_fence(DX::Fence* fence, u64 value_to_wait)
	{
		u64 completed_value = fence->ptr->GetCompletedValue();
		if (completed_value < value_to_wait)
		{
			THR(fence->ptr->SetEventOnCompletion(value_to_wait, fence->event));
			WaitForSingleObject(fence->event, INFINITE);
		}
	}

	internal void wait_for_work(Context* ctx)
	{
		u64 value_to_signal = signal(ctx);
		sync_with_fence(&ctx->fence, value_to_signal);
	}
	
	internal void execute_and_wait(Context* ctx)
	{
		THR(ctx->cmd_list->Close());
		ID3D12CommandList* const commandLists[] = { ctx->cmd_list };
		ctx->queue->ExecuteCommandLists(_countof(commandLists), commandLists);
		wait_for_work(ctx);
	}
	
	D3D12_INDEX_BUFFER_VIEW get_index_buffer_view(Buffer buf)
	{
		D3D12_INDEX_BUFFER_VIEW out{};
					
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		if (buf.stride_bytes == 2)
			format = DXGI_FORMAT_R16_UINT;
		else if (buf.stride_bytes == 4)
			format = DXGI_FORMAT_R32_UINT;
					
		assert(format != DXGI_FORMAT_UNKNOWN && "not supported stride!");
					
		out =
		{
			.BufferLocation = buf.ptr->GetGPUVirtualAddress(),
			.SizeInBytes = (UINT)buf.size_bytes,
			.Format = format
		};
					
		return out;
	}
				
	u32 get_count_indices(D3D12_INDEX_BUFFER_VIEW view)
	{
		u32 out = 0;
		u32 stride = 0;
					
		if (view.Format == DXGI_FORMAT_R16_UINT)
			stride = 2;
		else if (view.Format == DXGI_FORMAT_R32_UINT)
			stride = 4;
					
		assert(stride != 0 && "wrong view!");
					
		out = view.SizeInBytes / stride;
		return out;
	}
	
	//TODO: change to own string implementation later
	[[nodiscard]]
	u64 get_file_name(const wchar_t* path, wchar_t* out_result, u32 size) 
	{
		u64 out_chars = 0;
		
		const wchar_t* last_slash = wcsrchr(path, L'\\');
    
		if (last_slash == nullptr) 
		{
			last_slash = wcsrchr(path, L'/');
		}

		const wchar_t* file_name = (last_slash != nullptr) ? last_slash + 1 : path;
		const wchar_t* last_dot = wcsrchr(file_name, L'.');

		out_chars = (last_dot != nullptr) ? (u64)(last_dot - file_name) : wcslen(file_name);

		wcsncpy_s(out_result, size, file_name, out_chars);
		out_result[out_chars] = L'\0';
		
		return out_chars;
	}
	
	[[nodiscard]]
	internal IDxcBlob* compile_shader_default(LPCWSTR path, LPCWSTR name, LPCWSTR entry_point, LPCWSTR target)
	{
		IDxcBlob* out;
		
		constexpr u32 max_name_chars = 128;
		wchar_t buffor_pdb[max_name_chars];
		wchar_t buffor_bin[max_name_chars];
		
		swprintf_s(buffor_pdb, max_name_chars, L"%ls.pdb", name);
		swprintf_s(buffor_bin, max_name_chars, L"%ls.bin", name);
		
		LPCWSTR args[] =
		{
		name,										// Optional shader source file name for error reporting and for PIX shader source view.
		L"-E", entry_point,    	// Entry point.
		L"-T", target,					// Target.
		L"-Zs",									// Enable debug information (slim format)
		L"-D", L"MYDEFINE=1",		// A single define.
		L"-Fo", buffor_bin,			// Optional. Stored in the pdb. 
		L"-Fd", buffor_pdb,			// Name of the pdb. This must either be supplied or the autogenerated file name must be used.
		L"-Qstrip_reflect",			// Strip reflection into a separate blob. 
		L"-Zpc",
		L"-enable-16bit-types",
		};
		
		static IDxcCompiler3* 			dx_compiler = nullptr;
		static IDxcUtils* 					dx_utils = nullptr;
		static IDxcIncludeHandler* 	dx_incl_handler = nullptr;
		
		if (dx_compiler == nullptr)
		{
			THR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dx_utils)));
			THR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dx_compiler)));
			THR(dx_utils->CreateDefaultIncludeHandler(&dx_incl_handler));
		}
		
		IDxcResult* result = nullptr;
		IDxcBlobUtf8* error = nullptr;
		IDxcBlob* pShader = nullptr;
		IDxcBlob* pPDB = nullptr;
		 
		auto d = defer([&] { RELEASE_SAFE(result); RELEASE_SAFE(error); RELEASE_SAFE(pShader);  RELEASE_SAFE(pPDB);});

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
		
		// save binary
		{
			IDxcBlobUtf16* pShaderName = nullptr;
			result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), &pShaderName);
			if (pShader != nullptr)
			{
				FILE* fp = NULL;

				_wfopen_s(&fp, pShaderName->GetStringPointer(), L"wb");
				fwrite(pShader->GetBufferPointer(), pShader->GetBufferSize(), 1, fp);
				fclose(fp);
			}
		}
		
		// Save pdb
		{
			IDxcBlobUtf16* pPDBName = nullptr;
			result->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(&pPDB), &pPDBName);
			{
				FILE* fp = NULL;

				// Note that if you don't specify -Fd, a pdb name will be automatically generated.
				// Use this file name to save the pdb so that PIX can find it quickly.
				_wfopen_s(&fp, pPDBName->GetStringPointer(), L"wb");
				fwrite(pPDB->GetBufferPointer(), pPDB->GetBufferSize(), 1, fp);
				fclose(fp);
			}
		}
		
		THR(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&out), nullptr));
		
		return out;
	}
	
	[[nodiscard]]
	internal Upload_Heap create_upload_heap(ID3D12Device2* device, u64 max_bytes)
	{
		Upload_Heap out{};
		
		THR(device->CreateCommittedResource(get_cptr<D3D12_HEAP_PROPERTIES>({ .Type = D3D12_HEAP_TYPE_UPLOAD }),
		                                    D3D12_HEAP_FLAG_NONE,
		                                    get_cptr(CD3DX12_RESOURCE_DESC::Buffer(max_bytes)),
		                                    D3D12_RESOURCE_STATE_GENERIC_READ,
		                                    nullptr,
		                                    IID_PPV_ARGS(&out.heap)));
		
		THR(out.heap->Map(0, 
		                  get_cptr<D3D12_RANGE>({ .Begin = 0, .End = 0 }), 
		                  (void**) &out.heap_arena.base));
		
		out.gpu_base = out.heap->GetGPUVirtualAddress();
		out.heap_arena.max_size = max_bytes;
		
		return out;
	}
	
	[[nodiscard]]
	internal auto allocate_to_upload_heap(Upload_Heap* heap, u64 size)
	{
		struct { u8* addr_cpu; D3D12_GPU_VIRTUAL_ADDRESS addr_gpu; }out;
		
		out.addr_cpu = (u8*)allocate(&heap->heap_arena, size, g_alloc_alignment);
		out.addr_gpu = heap->gpu_base + heap->heap_arena.prev_offset; //prev offset was curr offset in cpu alloc
		
		return out;
	}
	
	internal D3D12_GPU_VIRTUAL_ADDRESS push_to_upload_heap(Upload_Heap* heap, Memory_View mem)
	{
		const auto [cpu_addr, gpu_addr] = allocate_to_upload_heap(heap, mem.bytes);
		memcpy(cpu_addr, mem.data, mem.bytes);
			
		return gpu_addr;
	}
	
	internal void reset_upload_heap(Upload_Heap* heap)
	{
		arena_reset_nz(&heap->heap_arena);
	}
	
	//TODO: return handle to resource
	internal Buffer create_buffer(ID3D12Device2* device, Memory_View mem)
	{
		Buffer out{.size_bytes = mem.bytes, .stride_bytes = mem.stride};
		
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(mem.bytes);
		THR(device->CreateCommittedResource(get_cptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
																				D3D12_HEAP_FLAG_NONE,
																				&desc,
																				D3D12_RESOURCE_STATE_COMMON,
																				nullptr,
																				IID_PPV_ARGS(&out.ptr)));
		
		return out;
	}
	
	internal u16 calc_mips(u32 width, u32 height)
	{
		u32 out = 1;
		// logic "or" both dimensions and divide by next powers of 2 by shifting right
		while ((width | height) >> out)
			++out;
		
		return (u16)out;
	}
	
	internal Texture create_texture(ID3D12Device2* device, Image_View img, u16 arr_size = 1, u16 mips = 0)
	{
		assert(arr_size == 1 || arr_size == 6);
		
		Texture out {
			.format = (DXGI_FORMAT)img.format,
			.width = img.width, 
			.height = img.height, 
		};
		
		u16 mip_levels = (mips > 0) ? mips : calc_mips(img.width, img.height);
		D3D12_RESOURCE_DESC desc = 	CD3DX12_RESOURCE_DESC::Tex2D(out.format, out.width, out.height, arr_size, mip_levels);
		THR(device->CreateCommittedResource(get_cptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
																				D3D12_HEAP_FLAG_NONE,
																				&desc,
																				D3D12_RESOURCE_STATE_COMMON,
																				nullptr,
																				IID_PPV_ARGS(&out.ptr)));
		
		return out;
	}
	
	//TODO: take handle to resource
	internal void push_to_default(Context* ctx, Buffer* buf, Upload_Heap* upload, Memory_View mem, D3D12_RESOURCE_STATES end_state)
	{
		auto gpu_h = push_to_upload_heap(upload, mem);
		u64 aligned_size = upload->heap_arena.curr_offset - upload->heap_arena.prev_offset;
		ctx->cmd_list->CopyBufferRegion(buf->ptr, 0, upload->heap, upload->heap_arena.prev_offset, aligned_size);
		ctx->cmd_list->ResourceBarrier(1, get_cptr(CD3DX12_RESOURCE_BARRIER::Transition
																																						 (buf->ptr, 
																																							D3D12_RESOURCE_STATE_COPY_DEST, 
																																							end_state)));
		buf->state = end_state;
	} 
	
	internal void push_texture_to_default(ID3D12Device2* device, Context* ctx, Texture* tex, 
																				Upload_Heap* upload, Memory_View mem, 
																				D3D12_RESOURCE_STATES end_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
																u32 num_subresources = 0)
	{
		// Assumed that subresources offsets and row sizes in mem are the same as from Footprints and no subresources depth
		u64 required_bytes = 0;
		u32 rows[g_max_count_texture_subresource]{};
		u64 row_bytes[g_max_count_texture_subresource]{};
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[g_max_count_texture_subresource]{};
		auto desc = tex->ptr->GetDesc();
		u32 count_subresources = (num_subresources > 0) ? num_subresources : desc.MipLevels * desc.DepthOrArraySize;
	
		device->GetCopyableFootprints(&desc, 0, count_subresources, 0, layouts, rows, row_bytes, &required_bytes);
		
		//TODO: more complete handling by Tardiff & MJP https://alextardif.com/D3D11To12P3.html
		u64 dst_res_start_offset = 0;
		for(u32 subres_i = 0; subres_i < count_subresources; ++subres_i)
		{
			byte* src_subres = (byte*)(mem.data) + layouts[subres_i].Offset;
			for(u32 row_i = 0; row_i < rows[subres_i]; ++row_i)
			{
				byte* src_row = src_subres + row_i * row_bytes[subres_i];
				push_to_upload_heap(upload, { src_row, row_bytes[subres_i] });
				u64 allocated_row = upload->heap_arena.curr_offset - upload->heap_arena.prev_offset;
				manual_offset(&upload->heap_arena, layouts[subres_i].Footprint.RowPitch - allocated_row);
				
				// get aligned start of the first subresource in upload
				if (row_i == 0 && subres_i == 0)
					dst_res_start_offset = upload->heap_arena.prev_offset;
			}
			
			//TODO: This is wrong, commented for now, fix!
			//u64 dst_subres_offset = upload->heap_arena.curr_offset - dst_res_start_offset;
			//manual_offset(&upload->heap_arena, layouts[subres_i].Offset - dst_subres_offset);
		}
		
		//TODO: (change) single CopyTextureRegion starting from first subresource instead of each invidually
		D3D12_TEXTURE_COPY_LOCATION src{};
		src.pResource = upload->heap;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = layouts[0];
		src.PlacedFootprint.Offset = dst_res_start_offset;
		
		ctx->cmd_list->CopyTextureRegion(
			get_cptr<D3D12_TEXTURE_COPY_LOCATION>({
				.pResource = tex->ptr,
				.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
				.SubresourceIndex = 0
		}),0,0,0,
			&src, nullptr );
		ctx->cmd_list->ResourceBarrier(1, get_cptr(CD3DX12_RESOURCE_BARRIER::Transition(tex->ptr, 
																																							 D3D12_RESOURCE_STATE_COPY_DEST, 
																																							 end_state)));
		tex->state = end_state;
	}
	
	//TODO: temporary function that handles all uplaoding to default of .dds straight from disk
	[[nodiscard]]
	internal Texture load_and_push_dds(ID3D12Device2* device, Context* ctx, const wchar_t* path, 
																		 D3D12_RESOURCE_STATES end_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
	{
		Texture out{};
		
		ID3D12Resource* tex;
		std::unique_ptr<uint8_t[]> dds_data;
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		THR( DirectX::LoadDDSTextureFromFile(device, path, &tex, dds_data, subresources));
	
		const u64 upload_size = GetRequiredIntermediateSize(tex, 0, (u32)(subresources.size()));

		// Create temporary upload heap
		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(upload_size);
		ID3D12Resource* temp_upload_heap;
		THR( device->CreateCommittedResource( &heapProps,
																					D3D12_HEAP_FLAG_NONE,
																					&desc,
																					D3D12_RESOURCE_STATE_GENERIC_READ,
																					nullptr,
																					IID_PPV_ARGS(&temp_upload_heap)));
		
		// Push to default
		UpdateSubresources(ctx->cmd_list, tex, temp_upload_heap,
											 0, 0, (u32)(subresources.size()), subresources.data());
		
		auto res_desc = tex->GetDesc();
		
		out = { tex, end_state, res_desc.Format, (u32)res_desc.Width, (u32)res_desc.Height, res_desc.MipLevels };
		
		ctx->cmd_list->ResourceBarrier(1, get_cptr(CD3DX12_RESOURCE_BARRIER::Transition(out.ptr, 
																																								D3D12_RESOURCE_STATE_COPY_DEST, 
																																								end_state)));
		return out;
	}
	
	[[nodiscard]]
	internal Descriptor_Heap create_descriptor_heap(ID3D12Device2* device, u32 count_descriptors, 
																				 D3D12_DESCRIPTOR_HEAP_TYPE type,
																				 D3D12_DESCRIPTOR_HEAP_FLAGS flags)
	{
		Descriptor_Heap out{};
			
		D3D12_DESCRIPTOR_HEAP_DESC desc
		{
			.Type						= type,
			.NumDescriptors	= count_descriptors,
			.Flags					= flags,
		};
		THR(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&out.heap)));
			
		out.base.h_cpu = out.heap->GetCPUDescriptorHandleForHeapStart();
		if (flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
			out.base.h_gpu = out.heap->GetGPUDescriptorHandleForHeapStart();

		out.max_count = count_descriptors;
		out.descriptor_size = device->GetDescriptorHandleIncrementSize(type);
			
		return out;
	}
	
	//TODO: For now, arena based allocation, maybe slot allocator would be better fit? (same size of descriptors)
	[[nodiscard]]
	internal Descriptor allocate_descriptor(Descriptor_Heap* heap)
	{
		assert(heap->max_count >= heap->count + 1 && "no space!");
		Descriptor out{};
		
		out.h_cpu = { .ptr = heap->base.h_cpu.ptr + heap->descriptor_size * heap->count };
		if(heap->base.h_gpu.ptr	!= 0)
			out.h_gpu = { .ptr = heap->base.h_gpu.ptr + heap->descriptor_size * heap->count };
		heap->count += 1;
		
		return out;
	}
	
	//TODO: take handle to resource
	internal Resource_View push_descriptor(ID3D12Device2* device, Descriptor_Heap* heap, ID3D12Resource* res, D3D12_SHADER_RESOURCE_VIEW_DESC desc)
	{
		Resource_View out {};
				
		Descriptor srv_h = allocate_descriptor(heap);
		out.id = heap->count - 1;
		out.desc = desc;
	
		device->CreateShaderResourceView(res, &out.desc, srv_h.h_cpu);
				
		return out;
	}
	
	[[nodiscard]]
	internal Descriptor get_descriptor(Descriptor_Heap* heap, u32 i)
	{
		assert(i < heap->count && i >= 0); 
		Descriptor out{};
			
		out.h_cpu = { .ptr = heap->base.h_cpu.ptr + heap->descriptor_size * heap->count };
		if(heap->base.h_gpu.ptr	!= 0)
			out.h_gpu = { .ptr = heap->base.h_gpu.ptr + heap->descriptor_size * heap->count };
			
		return out;
	}
	
	[[nodiscard]]
	internal u32 get_descriptor_id(Descriptor_Heap* heap, Descriptor* descriptor)
	{
		return (u32)((descriptor->h_cpu.ptr - heap->base.h_cpu.ptr) / heap->descriptor_size);
	}
	
	internal void reset_descriptor_heap(Descriptor_Heap* heap)
	{
		heap->count = 0;
	}
	
	//TODO: "state" ptr is not being used with global state approach
	internal void rhi_init(RHI_State* state, void* win_handle, u32 client_width, u32 client_height)
	{
		auto* ctx = &g_state.ctx_direct;
		
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
		THR(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&g_state.device)));
		#ifdef _DEBUG
		THR(g_state.device->QueryInterface(&g_state.d_dev));
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
			THR(g_state.device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&ctx->queue)));
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
			THR(temp_swapchain->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&g_state.swapchain));
			g_state.frame_index = g_state.swapchain->GetCurrentBackBufferIndex();
			
			temp_swapchain->Release();
		}
		
		g_state.rtv_heap = create_descriptor_heap(g_state.device, g_count_backbuffers,
																							D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
		g_state.dsv_heap = create_descriptor_heap(g_state.device, 1, 
																							D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
		
		// Create heaps
		{
			for(u32 frame_i = 0; frame_i < g_count_backbuffers; ++frame_i)
			{
				g_state.upload_heaps[frame_i] = create_upload_heap(g_state.device, g_upload_heap_max_size);
				g_state.cbv_srv_uav_heap[frame_i] = create_descriptor_heap(g_state.device, g_max_count_cbv_srv_uav_descriptors,
																																	 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
																																	 D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
			}
		}
		
		// Create fence
		THR(g_state.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx->fence.ptr)));
		ctx->fence.event = CreateEvent(0, false, false, 0);
		AlwaysAssert(ctx->fence.event && "Failed creation of fence event");
		
		// Create Command Allocators
		for (u32 i = 0; i < g_count_backbuffers; i++)
		{
			THR(g_state.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
			                                   IID_PPV_ARGS(&ctx->cmd_allocators[i])));
			THR(ctx->cmd_allocators[i]->Reset());
		}
		
		// Create Initial Command List
		THR(g_state.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
		                              ctx->cmd_allocators[g_state.frame_index], nullptr,
		                              IID_PPV_ARGS(&ctx->cmd_list)));
		THR(ctx->cmd_list->Close());
		THR(ctx->cmd_list->Reset(ctx->cmd_allocators[g_state.frame_index], nullptr));
		
		g_state.is_initalized = true;
	}
	
	//TODO: later can provide desingated struct with changes
	D3D12_GRAPHICS_PIPELINE_STATE_DESC create_default_pipeline_desc()
	{
		return {
			.pRootSignature = nullptr,
			.VS = {},
			.PS = {},
			.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
			.SampleMask = UINT_MAX,
			.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
			.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
			.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
			.NumRenderTargets = 1,
			.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
			.DSVFormat = DXGI_FORMAT_D32_FLOAT,
			.SampleDesc = {.Count = 1},
		};
	}
	
	[[nodiscard]]
	internal Pipeline create_render_pipeline(ID3D12Device2* device, const wchar_t* vs_ps_path)
	{
		Pipeline out{};
		
		constexpr u32 max_name_chars = 128;
		wchar_t buffer_name[max_name_chars];
		u64 name_chars = get_file_name(vs_ps_path, buffer_name, max_name_chars);
		swprintf_s(buffer_name, max_name_chars - name_chars, L"%ls_ps", buffer_name);
		
		// Compile shaders
		auto pixel_shader = compile_shader_default(vs_ps_path, buffer_name, L"PSMain", L"ps_6_6");
		buffer_name[name_chars + 1] = L'v';
		buffer_name[name_chars + 2] = L's';
		auto vertex_shader = compile_shader_default(vs_ps_path, buffer_name, L"VSMain", L"vs_6_6");
		
		auto d = defer([&] { RELEASE_SAFE(pixel_shader); RELEASE_SAFE(vertex_shader);});
		
		// Create root signature from shader
		THR(device->CreateRootSignature(0, vertex_shader->GetBufferPointer(), 
			                            	vertex_shader->GetBufferSize(), IID_PPV_ARGS(&out.root_signature)));
		
		// Create PSO
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = create_default_pipeline_desc();
			pso_desc.pRootSignature = out.root_signature;
			pso_desc.VS = { vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize() };
			pso_desc.PS = { pixel_shader->GetBufferPointer(),  pixel_shader->GetBufferSize() };
			pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			
			// For RH coordinate system, CCW winding order is needed
			pso_desc.RasterizerState.FrontCounterClockwise = TRUE;
			
			THR(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&out.pso)));
		}
		
		return out;
	}
} // namespace DX

extern void rhi_run(Data_To_RHI* data_from_app, Game_Window* window)
{
	using namespace DX;
	
	if (!g_state.is_initalized)
	{
		rhi_init(&g_state, window->handle, window->width, window->height);
	}
	// TEMPORARY helper autos, wish to have Odin/Jai "using" feature
	auto* ctx			= &g_state.ctx_direct; // for now only direct context
	auto* device	= g_state.device;
		
	auto& width		= g_state.width;
	auto& height	= g_state.height;
		
	auto* swapchain 	= g_state.swapchain;
	auto* rtv_heap 		= &g_state.rtv_heap;
	auto rtv_texture	= g_state.rtv_texture;
	auto* dsv_heap 		= &g_state.dsv_heap;
	auto& dsv_texture	= g_state.dsv_texture;
		
	auto& fence_signals = g_state.fence_signals;
	auto& frame_index 	= g_state.frame_index;
	
	auto& vertices_static = g_state.vertices_static;
	auto& indices_static 	= g_state.indices_static;
	auto& attr_static 		= g_state.attrs_static;
	
	auto& albedo_static 	= g_state.albedo_static;
	auto& normal_static 	= g_state.normal_static;
	auto& rough_static 		= g_state.rough_static;
	auto& ao_static 			= g_state.ao_static;
	
	auto& env 						= g_state.env;
	auto& env_irr 				= g_state.env_irr;
	
	auto& default_pso 		= g_state.default_pso;
	auto& skybox_pso 			= g_state.skybox_pso;
	
	auto* upload_heap = &g_state.upload_heaps[frame_index];

	// Static data upload
	if (data_from_app->is_new_static) 
	{
		//TODO: should clear old ones ( wait till all frames finished before clear)
		
		// Create static shaders & psos
		default_pso = create_render_pipeline(device, data_from_app->shader_path);
		skybox_pso = create_render_pipeline(device, L"../source/shaders/skybox.hlsl");
		
		// Create & push static buffers
		vertices_static = create_buffer(device, data_from_app->st_geo.positions);
		push_to_default(ctx, &vertices_static, upload_heap, data_from_app->st_geo.positions, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		
		indices_static = create_buffer(device, data_from_app->st_geo.indices);
		push_to_default(ctx, &indices_static, upload_heap, data_from_app->st_geo.indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		
		Memory_View attr_mem = data_from_app->st_geo.attributes.get_memory_view();
		attr_static = create_buffer(device, attr_mem);
		push_to_default(ctx, &attr_static, upload_heap, attr_mem, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		
		// Create & push albedo
		albedo_static = create_texture(device, data_from_app->st_albedo, 1, 1);
		push_texture_to_default(device, ctx, &albedo_static, upload_heap, data_from_app->st_albedo.mem);
		// Create & push normal
		normal_static = create_texture(device, data_from_app->st_normal, 1, 1);
		push_texture_to_default(device, ctx, &normal_static, upload_heap, data_from_app->st_normal.mem);
		// Create & push roughness
		rough_static = create_texture(device, data_from_app->st_roughness, 1, 1);
		push_texture_to_default(device, ctx, &rough_static, upload_heap, data_from_app->st_roughness.mem);
		// Create & push ambient occlusion
		ao_static = create_texture(device, data_from_app->st_ao, 1, 1);
		push_texture_to_default(device, ctx, &ao_static, upload_heap, data_from_app->st_ao.mem);
		
		env = load_and_push_dds(device, ctx, L"../assets/resting.dds");
		env_irr = load_and_push_dds(device, ctx, L"../assets/resting_IR.dds");
		
		execute_and_wait(ctx);
	}
	
	// RTV & DSV check for re/creation
	if (window->width != width || window->height != height || !rtv_texture[0])
	{
		width	= window->width;
		height = window->height;
			
		wait_for_work(ctx);
		
		// Clean
		{
			reset_descriptor_heap(rtv_heap);
			reset_descriptor_heap(dsv_heap);
			for (u32 i = 0; i < g_count_backbuffers; i++)
			{
				RELEASE_SAFE(rtv_texture[i]);
			}
			RELEASE_SAFE(dsv_texture.ptr);
		}

		// Resize swap chain
		{
			DXGI_SWAP_CHAIN_DESC swapchain_desc{};
			THR(swapchain->GetDesc(&swapchain_desc));
			THR(swapchain->ResizeBuffers(g_count_backbuffers, width, height, 
																	 swapchain_desc.BufferDesc.Format, swapchain_desc.Flags));
			
			frame_index = swapchain->GetCurrentBackBufferIndex();
		}
			
		// Create/Update RTV textures
		{
			D3D12_RENDER_TARGET_VIEW_DESC rtv_desc
			{
				.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, // Sync This!
				.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D 
			};
			for (u32 i = 0; i < g_count_backbuffers; i++)
			{
				THR(swapchain->GetBuffer(i, IID_PPV_ARGS(&rtv_texture[i])));
				device->CreateRenderTargetView(rtv_texture[i], &rtv_desc, allocate_descriptor(rtv_heap).h_cpu);
			}
		}
				
		// Create/Update DSV, depth texture
		{
			auto desc = CD3DX12_RESOURCE_DESC::Tex2D
								(DXGI_FORMAT_D32_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
			dsv_texture.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
			dsv_texture.format = desc.Format;
			dsv_texture.width = width;
			dsv_texture.height = height;
				
			THR(device->CreateCommittedResource(
				get_cptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
				D3D12_HEAP_FLAG_NONE,
				&desc,
				dsv_texture.state,
				get_cptr(CD3DX12_CLEAR_VALUE(desc.Format, 1.0f, 0)),
				IID_PPV_ARGS(&dsv_texture.ptr)
			));
					
			// Create/Update descriptor in dsv_heap
			{
				D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc 
				{
					.Format 						=	DXGI_FORMAT_D32_FLOAT, // Sync This!
					.ViewDimension 			= D3D12_DSV_DIMENSION_TEXTURE2D,
					.Flags 							= D3D12_DSV_FLAG_NONE,
					.Texture2D {.MipSlice = 0}
				};
				device->CreateDepthStencilView(dsv_texture.ptr, &dsv_desc, 
																			 allocate_descriptor(dsv_heap).h_cpu);
			}
		}
	}
			
	// D3D12 render
	{
		auto cmd_alloc = ctx->cmd_allocators[frame_index];
		auto backbuffer = rtv_texture[frame_index];
		auto* cbv_srv_uav_heap = &g_state.cbv_srv_uav_heap[frame_index];
			
		THR(cmd_alloc->Reset());
		// Reset current command list taken from current command allocator
		THR(ctx->cmd_list->Reset(cmd_alloc, default_pso.pso));
			
		// Clear rtv & dsv
		{
			ctx->cmd_list->ResourceBarrier(1, get_cptr(CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
			                                                                                D3D12_RESOURCE_STATE_PRESENT,
			                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET)));
			lib::Vec4 color { 0.262f, 0.514f, 0.672f, 1.0f };
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(rtv_heap->base.h_cpu,
			                                         frame_index, rtv_heap->descriptor_size);
				
			ctx->cmd_list->ClearRenderTargetView(rtv_handle, color.e, 0, nullptr);
			ctx->cmd_list->ClearDepthStencilView(dsv_heap->base.h_cpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			ctx->cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_heap->base.h_cpu);
		}
			
		D3D12_GPU_VIRTUAL_ADDRESS cbv_gpu_addr_frame = push_to_upload_heap(upload_heap, data_from_app->cb_frame);
		D3D12_GPU_VIRTUAL_ADDRESS cbv_gpu_addr_draw = push_to_upload_heap(upload_heap, data_from_app->cb_draw);
		
		//TODO: This will need proper refactor along with whole resource/view/descriptor_heap managment
		Resource_View view_verts = push_descriptor(device, cbv_srv_uav_heap, vertices_static.ptr, 
																								 {
																										.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
																										.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																										.Buffer = {
																															.FirstElement = 0,
																															.NumElements = (u32)vertices_static.size_bytes / vertices_static.stride_bytes,
																															.StructureByteStride = vertices_static.stride_bytes,
																															.Flags = D3D12_BUFFER_SRV_FLAGS::D3D12_BUFFER_SRV_FLAG_NONE 
																									}});
			
		Resource_View view_attrs = push_descriptor(device, cbv_srv_uav_heap, attr_static.ptr, 
																							 {
																								 .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
																								 .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																								 .Buffer = {
																													 .FirstElement = 0,
																													 .NumElements = (u32)attr_static.size_bytes / attr_static.stride_bytes,
																													 .StructureByteStride = attr_static.stride_bytes,
																													 .Flags = D3D12_BUFFER_SRV_FLAGS::D3D12_BUFFER_SRV_FLAG_NONE 
																								 }});
		
		Resource_View view_tex_albedo = push_descriptor(device, cbv_srv_uav_heap, albedo_static.ptr, 
																										{
																											.Format = albedo_static.format,
																											.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
																											.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																											.Texture2D = {.MipLevels = 1}
																										});
		
		Resource_View view_tex_normal = push_descriptor(device, cbv_srv_uav_heap, normal_static.ptr, 
																									 {
																										 .Format = normal_static.format,
																										 .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
																										 .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																										 .Texture2D = {.MipLevels = 1}
																									 });
		
		Resource_View view_tex_rough = push_descriptor(device, cbv_srv_uav_heap, rough_static.ptr, 
																										{
																											.Format = rough_static.format,
																											.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
																											.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																											.Texture2D = {.MipLevels = 1}
																										});
		
		Resource_View view_ao = push_descriptor(device, cbv_srv_uav_heap, ao_static.ptr, 
																									 {
																										 .Format = ao_static.format,
																										 .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
																										 .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																										 .Texture2D = {.MipLevels = 1}
																									 });
		
		Resource_View view_env = push_descriptor(device, cbv_srv_uav_heap, env.ptr, 
																						{
																							.Format = env.format,
																							.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE,
																							.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																							.TextureCube = {.MipLevels = env.mips}
																						});
		
		Resource_View view_env_irr = push_descriptor(device, cbv_srv_uav_heap, env_irr.ptr, 
																						 {
																							 .Format = env_irr.format,
																							 .ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE,
																							 .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
																							 .TextureCube = {.MipLevels = env_irr.mips}
																						 });
		
		// Populate command list
		{
			// Default state
			ctx->cmd_list->RSSetViewports(1, get_cptr(CD3DX12_VIEWPORT(0.0f, 0.0f, (f32)width, (f32)height)));
			ctx->cmd_list->RSSetScissorRects(1, get_cptr(CD3DX12_RECT(0, 0, (u32)width, (u32)height)));
			ctx->cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->cmd_list->SetDescriptorHeaps(1, &cbv_srv_uav_heap->heap);
			
			// Drawing static data
			{
				ctx->cmd_list->SetGraphicsRootSignature(default_pso.root_signature);
				ctx->cmd_list->SetGraphicsRootConstantBufferView(2, cbv_gpu_addr_frame);
				ctx->cmd_list->SetGraphicsRootConstantBufferView(1, cbv_gpu_addr_draw);
				//TODO: find a way to generalize this
				ctx->cmd_list->SetGraphicsRoot32BitConstants(0, sizeof(Draw_Ids) / sizeof(u32),
																										 get_cptr(Draw_Ids{
																											view_verts.id, 
																											view_attrs.id,
																											view_tex_albedo.id,
																											view_tex_normal.id,
																											view_tex_rough.id,
																											view_ao.id,
																											view_env.id,
																											view_env_irr.id
																										}), 0);
				
				auto view_indices = get_index_buffer_view(indices_static);
				ctx->cmd_list->IASetIndexBuffer(&view_indices);
				ctx->cmd_list->DrawIndexedInstanced(get_count_indices(view_indices), 1, 0, 0, 0);
			}
			
			// Drawing skybox
			{
				ctx->cmd_list->SetPipelineState(skybox_pso.pso);
				ctx->cmd_list->DrawInstanced(3, 1, 0, 0);
			}
		}
			
		// Present
		{
			ctx->cmd_list->ResourceBarrier(1, get_cptr
			                                (CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, 
			                                                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
			                                                                      D3D12_RESOURCE_STATE_PRESENT)));
			THR(ctx->cmd_list->Close());
				
			ID3D12CommandList* const commandLists[] = { ctx->cmd_list };
			ctx->queue->ExecuteCommandLists(_countof(commandLists), commandLists);
				
			// Present current backbuffer
			THR(swapchain->Present(1, 0));
				
			// Signal the end of direct context work for this frame and store its value for later synchronization
			fence_signals[frame_index] = signal(ctx);
				
			// Get next backbuffer index - updated on swapchain by Present()
			frame_index = swapchain->GetCurrentBackBufferIndex();
				
			// Wait for fence value from next frame from swapchain - not waiting on current frame
			sync_with_fence(&ctx->fence, fence_signals[frame_index]);
			// Heaps from next frame are safe to reset cause work from that frame has finished
			reset_upload_heap(&g_state.upload_heaps[frame_index]);
			reset_descriptor_heap(&g_state.cbv_srv_uav_heap[frame_index]);
		}
	}
}