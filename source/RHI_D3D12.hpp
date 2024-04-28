#pragma once

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

struct Data_To_RHI;

// internal structures
namespace DX
{
	struct Descriptor 
	{
    CD3DX12_CPU_DESCRIPTOR_HANDLE h_cpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE h_gpu;
	};
	
	struct Fence
	{
		ID3D12Fence* ptr;
		u64 counter;
		HANDLE event;
	};
	
	struct Memory_Heap
	{
		Alloc_Arena heap_arena;
		ID3D12Resource* heap;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_base;
	};
	
	struct Descriptor_Heap
	{
		ID3D12DescriptorHeap* heap;
		Descriptor base;
		u32 count;
		u32 max_count;
		u32 descriptor_size;
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

struct Context
{
	ID3D12CommandQueue* queue;
	ID3D12GraphicsCommandList* cmd_list;
	ID3D12CommandAllocator* cmd_allocators[g_count_backbuffers]{};
	
	DX::Fence fence;
};

struct RHI_State
{
	// Logical State
	ID3D12Device2* device;
	ID3D12DebugDevice2* d_dev;
	Context ctx_direct;
	
	IDXGISwapChain4* swapchain;
	ID3D12Resource* rtv_texture[g_count_backbuffers]{};
	DX::Descriptor_Heap rtv_heap;
	
	GPU_Resource dsv_texture;
	DX::Descriptor_Heap dsv_heap;
	
	DX::Memory_Heap upload_heaps[g_count_backbuffers];
	DX::Descriptor_Heap cbv_srv_uav_heap[g_count_backbuffers];
	
	u32 frame_index;
	u64 fence_values[g_count_backbuffers];
	
	b32 is_initalized;
	b32 vsync;
	u32 width;
	u32 height;
	
	// Data State
	GPU_Resource vertices_static;
	GPU_Resource indices_static;
	Pipeline static_pso;
};

namespace DX
{
	template <typename T>
	[[nodiscard]]
	GPU_Resource upload_static_data(ID3D12Device2* device, Context* ctx, Array_View<T>data_cpu)
	{
		auto cmd_list = ctx->cmd_list;
		GPU_Resource out{};
		
		ID3D12Resource* vb_staging = nullptr;
		const u32 buffer_size = data_cpu.size * sizeof(T);
		const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);
			
		THR(device->CreateCommittedResource(get_cptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)),
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
		THR(device->CreateCommittedResource(get_cptr(CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT)),
																				D3D12_HEAP_FLAG_NONE,
																				&desc,
																				D3D12_RESOURCE_STATE_COMMON,
																				nullptr,
																				IID_PPV_ARGS(&out.ptr)));
			
		cmd_list->CopyResource(out.ptr, vb_staging);
		cmd_list->ResourceBarrier(1, get_cptr(CD3DX12_RESOURCE_BARRIER::Transition
																																					(out.ptr, 
																																						D3D12_RESOURCE_STATE_COPY_DEST, 
																																						out.state)));
		return out;
	}
	
} // Namespace DX