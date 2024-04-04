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
		                 Vertex{ {	-0.5f, 0.5f, -2.5f, 1.0f },	{ 1.0f, 0.0f, 0.0f, 1.0f } },
										 Vertex{ {	0.5f, -0.5f, -1.5f, 1.0f },	{ 0.0f, 1.0f, 0.0f, 1.0f } },
										 Vertex{ { -0.5f, -0.5f, -1.5f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.5f,  0.5f, -2.5f, 1.0f }, { 0.0f, 1.0f, 1.0f, 1.0f } },
		
										 Vertex{ { -3.95f, 0.75f,	-4.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,	 0.0f,	-4.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ { -3.95f, 0.0f,	-4.7f, 1.0f },	{ 0.0f, 0.0f, 1.0f, 1.0f } },
										 Vertex{ {	0.0f,  0.75f,	-4.7f, 1.0f }, 	{ 0.0f, 0.0f, 1.0f, 1.0f } });
		
		Array_View<u16>indices_data{};
		indices_data.init(&app_state->arena_assets, 0, 1, 2,  0, 3, 1);
		
		rhi_init(rhi, window->handle, window->width, window->height);
		data_to_rhi->default_pipeline = create_basic_pipeline(device, L"../source/shaders/simple.hlsl");
		
		data_to_rhi->verts		=		upload_static_data(device, ctx_direct, vertex_data);
		data_to_rhi->indices	=		upload_static_data(device, ctx_direct, indices_data);
		execute_and_wait(ctx_direct);
		
		app_state->camera = { .pos = { 2.0f, 0.0f, 2.0f }, .yaw = PI32 + PI32 / 4.0f };
		
		arena_reset(&app_state->arena_assets);
		memory->is_initalized = true;
	}
	
	// Modyfing/Creating data for RHI
	{
		auto cam_yaw = app_state->camera.yaw;
		auto cam_pitch = app_state->camera.pitch;
		// Could also multiply pre-setted forward vec with rotation matrix around X(from pitch) and Y(from yaw)
		app_state->camera.forward = lib::normalize( lib::Vec3{
		                            .x = cos(cam_yaw) * cos(cam_pitch),
																.y = sin(cam_pitch),
																.z = sin(cam_yaw) * cos(cam_pitch)
		});
	
		lib::Mat4 mat_view = lib::create_look_at( { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		lib::Mat4 mat_trans = lib::create_translate({-0.33f, 0.0f, 0.0f});
		lib::Mat4 mat_rotation = lib::create_rotation_z((f32)window->time_ms / 1000.0f);
		lib::Mat4 mat_projection = lib::create_perspective(lib::deg_to_rad(50.0f), 
		                                                   (f32)window->width/window->height, 
		                                                   0.1f, 
		                                                   100.0f);
		lib::Mat4 mat_model = mat_projection * mat_view  * mat_rotation;
		
		f32 pulse = (f32)(std::sin((f32)window->time_ms / 300) + 1) / 2;
		
		// Pushing data
		{
			// General data
			data_to_rhi->time_passed_ms = window->time_ms;
			// Frame constants
			data_to_rhi->cb_frame.light_pos = lib::normalize(lib::Vec4 { -6.0f, 1.0f, -6.0f, 1.0f });
			data_to_rhi->cb_frame.light_col = { pulse, pulse, pulse, 1.0f };
			// Draw constants
			data_to_rhi->cb_draw.obj_to_world = mat_model;
		}
	}
	
	render_frame(rhi, data_to_rhi, window->width, window->height);
}