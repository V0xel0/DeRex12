#pragma once

#include "d3dx12.h"
#ifdef _DEBUG
#include <dxgidebug.h>
#endif
#include <dxgi1_6.h>

inline constexpr u8 g_count_backbuffers = 3;
inline constexpr u32 max_temp_barriers = 32;

struct Data_To_RHI;

// internal structures
namespace DX
{
	struct Descriptor 
	{
    CD3DX12_CPU_DESCRIPTOR_HANDLE h_cpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE h_gpu;
	};
	
	struct Resource_View
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc;
		u32 id;
	};
	
	struct Fence
	{
		ID3D12Fence* ptr;
		u64 counter;
		HANDLE event;
	};
	
	struct Upload_Heap
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
struct Buffer
{
	ID3D12Resource* ptr;
	D3D12_RESOURCE_STATES state;
	u64 size_bytes;
	u32 stride_bytes;
};

struct Texture
{
	ID3D12Resource* ptr;
	D3D12_RESOURCE_STATES state;
	DXGI_FORMAT format;
	u32 width;
	u32 height;
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
	
	//TODO: Just for now, later memory from platform?
	D3D12_RESOURCE_BARRIER bar_buffor[max_temp_barriers];
	Array_View<D3D12_RESOURCE_BARRIER>temp_barriers { .size = max_temp_barriers, .data = bar_buffor };
	
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
	
	Texture dsv_texture;
	DX::Descriptor_Heap dsv_heap;
	
	DX::Upload_Heap upload_heaps[g_count_backbuffers];
	DX::Descriptor_Heap cbv_srv_uav_heap[g_count_backbuffers];
	
	u32 frame_index;
	u64 fence_signals[g_count_backbuffers];
	
	b32 is_initalized;
	b32 vsync;
	u32 width;
	u32 height;
	
	// Data State
	Buffer vertices_static;
	Buffer indices_static;
	Buffer uvs_static; // temporary
	Texture albedo_static;
	Pipeline static_pso;
};
