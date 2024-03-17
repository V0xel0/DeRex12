#include <cassert>

#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Views.hpp"
#include "Math.hpp"

// Later different RHI possible
#define RHI_D3D12
#ifdef RHI_D3D12
#include "RHI_D3D12.hpp"
#include "RHI_D3D12.cpp"
using namespace DX;
#endif

#include "Game_Services.hpp"
#include "Render_Data.hpp"
#include "App.hpp"

extern "C" APP_FULL_UPDATE(app_full_update)
{
	App_State* app_state = (App_State*)memory->permanent_storage;
	auto* rhi 				= &app_state->rhi;
	auto* device 			= &app_state->rhi.device;
	auto* ctx_direct 	= &app_state->rhi.ctx_direct;
	auto* gpu_static 	= &app_state->gpu_static;
	
	if (!memory->is_initalized)
	{
		app_state->arena_persist.max_size = memory->size_permanent_storage - sizeof(App_State);
		app_state->arena_persist.base 		= (byte*)memory->permanent_storage + sizeof(App_State);
		app_state->arena_assets.max_size 	= memory->size_transient_storage;
		app_state->arena_assets.base 			= (byte*)memory->transient_storage;
		
		// Static data CPU creation
		Array_View<Vertex>vertex_data{};
		vertex_data.init(&app_state->arena_assets, 
		                 Vertex{ {	-0.5f, 0.5f, 0.5f, 1.0f },	{ 1.0f, 0.0f, 0.0f, 1.0f } },
										 Vertex{ {	0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 1.0f, 0.0f, 1.0f } },
										 Vertex{ { -0.5f, -0.5f, 0.5f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.5f,  0.5f, 0.5f, 1.0f }, 	{ 0.0f, 1.0f, 1.0f, 1.0f } },
		
										 Vertex{ { -0.75f, 0.75f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,	 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ { -0.75f, 0.0f,	0.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,  0.75f,	0.7f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } });
		
		Array_View<u16>indices_data{};
		indices_data.init(&app_state->arena_assets, 0, 1, 2,  0, 3, 1);
		
		rhi_init(rhi, window->handle, window->width, window->height);
		gpu_static->default_pipeline = create_basic_pipeline(device);
		
		gpu_static->verts  = upload_static_data(vertex_data,  ctx_direct, device);
		gpu_static->indices = upload_static_data(indices_data, ctx_direct, device);
		execute_and_wait(ctx_direct);
		
		arena_reset(&app_state->arena_assets);
		memory->is_initalized = true;
	}
	
	render_frame(rhi, gpu_static, window->width, window->height);
}