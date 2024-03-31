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
using namespace DX;
#endif

#include "Game_Services.hpp"
#include "Render_Data.hpp"
#include "App.hpp"

extern "C" void app_full_update(Game_Memory *memory, Game_Window *window, Game_Input *inputs)
{
	App_State* app_state = (App_State*)memory->permanent_storage;
	auto* rhi 				= &app_state->rhi;
	auto* device 			= &app_state->rhi.device;
	auto* ctx_direct 	= &app_state->rhi.ctx_direct;
	auto* data_to_rhi = &app_state->data_for_rhi;
	
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
		data_to_rhi->default_pipeline = create_basic_pipeline(device, L"../source/shaders/simple.hlsl");
		
		data_to_rhi->verts		=		upload_static_data(device, ctx_direct, vertex_data);
		data_to_rhi->indices	=		upload_static_data(device, ctx_direct, indices_data);
		execute_and_wait(ctx_direct);
		
		arena_reset(&app_state->arena_assets);
		memory->is_initalized = true;
	}
	
	data_to_rhi->time_passed_ms = window->time_passed;
	
	render_frame(rhi, data_to_rhi, window->width, window->height);
}