#pragma once

// Header represent functions and types that need to be implemented by every RHI
// Only external (non-namespaced) structures and functions needs to be implemented by RHI implementation

/* TODOs
 * 
 * 	0) Consider moving whole RHI to different TLU or part of platform
 * 				ideally just get "what to render" (unified msg) from application and own RHI-state memory
 * 						basically similar in desing to application itself
 *	1) Pipeline abstraction - bundle with root signature
 *	2) Heaps + descriptors creation & abstraction
 *	3) Pools + handles
 *	4) Data manager for GPU/CPU renderable resources
 *	5) Synchro functions, parameters abstraction
 *	6) Shader abstraction
 *	7) Render() split
 * 	8) Shader compilation improvements: reflection, signature from shader, output debug
 * 	9) Shader creation code compression, caching for results, save pdb's and reflection, recompile if new hash
 * 10) "Pipeline" struct may be changed to binary relation 1..n of '1' root signature to 'n' PSOs
 * 				This could be done as 2D array instead of generic solution like: https://github.com/RonPieket/BinaryRelations
 * 11) Maybe also reset gpu_memory in "execute_and_wait"?
 * 
 * */

#include "d3dx12.h"
#ifdef _DEBUG
#include <dxgidebug.h>
#endif
#include <dxgi1_6.h>

#ifdef _DEBUG
inline void THR(HRESULT hr) {
	AlwaysAssert(SUCCEEDED(hr));
}
#else
inline void THR(HRESULT) {}
#endif
		
#define RELEASE_SAFE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

inline constexpr u8 g_count_backbuffers = 3;
inline constexpr u32 g_alloc_alignment = 256;
inline constexpr u64 g_upload_heap_max_size = MiB(20);

struct Data_To_RHI;

// internal structures
namespace DX
{
	struct Descriptor 
	{
    D3D12_CPU_DESCRIPTOR_HANDLE h_cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE h_gpu;
	};
	
	struct Fence
	{
		ID3D12Fence* ptr;
		u64 fence_counter;
		HANDLE fence_event;
	};
	
	struct Memory_Heap
	{
		Alloc_Arena heap_arena;
		ID3D12Resource* heap;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_base;
	};
} // namespace DX

// external structures
struct GPU_Resource
{
	ID3D12Resource* ptr;
	D3D12_RESOURCE_STATES state;
	D3D12_RESOURCE_DESC desc;
};

struct Pipeline
{
	ID3D12PipelineState* pso;
	ID3D12RootSignature* root_signature;
};

struct Device
{
	ID3D12Device2* ptr;
	ID3D12DebugDevice2* d_ptr;
};

struct Context
{
	ID3D12CommandQueue* queue;
	ID3D12GraphicsCommandList* cmd_list;
	ID3D12CommandAllocator* cmd_allocators[g_count_backbuffers]{};
	
	DX::Fence fence;
};
 
struct RHI_State
{
	Device device;
	Context ctx_direct;
	
	IDXGISwapChain4* swapchain;
	ID3D12Resource* rtv_texture[g_count_backbuffers]{};
	ID3D12DescriptorHeap* rtv_heap;
	u32 rtv_descriptor_size;
	
	GPU_Resource dsv_texture;
	ID3D12DescriptorHeap* dsv_heap;
	
	DX::Memory_Heap upload_heaps[g_count_backbuffers];
	
	u32 frame_index;
	u64 fence_values[g_count_backbuffers];
	
	b32 is_initalized;
	b32 vsync;
	u32 width;
	u32 height;
};

namespace DX
{
	extern void rhi_init(RHI_State* state, void* win_handle, u32 client_width, u32 client_height);
	extern void render_frame(RHI_State* state, Data_To_RHI* gpu_static, u32 new_width, u32 new_height);
	extern void execute_and_wait(Context* ctx);
	
	extern Pipeline create_basic_pipeline(Device* dev, const wchar_t* vs_ps_path);
	
	template <typename T>
	[[nodiscard]]
	GPU_Resource upload_static_data(Device* dev, Context* ctx, Array_View<T>data_cpu)
	{
		auto cmd_list = ctx->cmd_list;
		auto device = dev->ptr;
		GPU_Resource out{};
		
		ID3D12Resource* vb_staging = nullptr;
		const u32 buffer_size = data_cpu.size * sizeof(T);
		const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);
			
		THR(device->CreateCommittedResource(get_const_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
																				D3D12_HEAP_FLAG_NONE,
																				&desc,
																				D3D12_RESOURCE_STATE_GENERIC_READ,
																				nullptr,
																				IID_PPV_ARGS(&vb_staging)));
			
		void* ptr = nullptr;
		CD3DX12_RANGE range(0, 0);
		THR(vb_staging->Map(0, &range, &ptr));
		memcpy(ptr, data_cpu.data, buffer_size);
		vb_staging->Unmap(0, nullptr);
	
		out.desc = desc;
		out.state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		THR(device->CreateCommittedResource(get_const_ptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
																				D3D12_HEAP_FLAG_NONE,
																				&desc,
																				D3D12_RESOURCE_STATE_COMMON,
																				nullptr,
																				IID_PPV_ARGS(&out.ptr)));
			
		cmd_list->CopyResource(out.ptr, vb_staging);
		cmd_list->ResourceBarrier(1, get_const_ptr(CD3DX12_RESOURCE_BARRIER::Transition
																																					(out.ptr, 
																																						D3D12_RESOURCE_STATE_COPY_DEST, 
																																						out.state)));
		return out;
	}
	
} // Namespace DX