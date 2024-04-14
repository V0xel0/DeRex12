#include <cassert>

#include "Utils.hpp"
#include "Allocators.hpp"
#include "GameAsserts.hpp"
#include "Views.hpp"
#include "Math.hpp"

#pragma warning(push, 0)   
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#pragma warning(pop)

#include "Game_Services.hpp"
#include "Render_Data.hpp"
#include "App.hpp"

inline internal lib::Vec3 move_camera(lib::Vec3 cam_pos, lib::Vec3 dir, f32 speed = 0.15f)
{
	lib::Vec3 out = cam_pos;
	
	return out + dir * speed;
}

extern "C" Data_To_RHI* app_full_update(Game_Memory *memory, Game_Window *window, Game_Input *inputs)
{
	App_State* app_state = (App_State*)memory->permanent_storage;
	auto* data_to_rhi = &app_state->data_for_rhi;
	auto* camera 			= &app_state->camera;
	app_state->data_for_rhi = {}; // zero every time
	
	if (!memory->is_initalized)
	{
		app_state->arena_persist.max_size = memory->size_permanent_storage - sizeof(App_State);
		app_state->arena_persist.base 		= (byte*)memory->permanent_storage + sizeof(App_State);
		app_state->arena_assets.max_size 	= memory->size_transient_storage;
		app_state->arena_assets.base 			= (byte*)memory->transient_storage;
		
		// Static data CPU creation
		Array_View<Vertex>vertex_data{};
		vertex_data.init(&app_state->arena_assets, 
		              Vertex{ {-1.f, -1.f, -1.f, 1.0f},	{0.f, 0.f, 0.f, 1.0f} }, // 0
									Vertex{ {-1.f, 1.f, -1.f, 1.0f},	{0.f, 1.f, 0.f, 1.0f} }, // 1
									Vertex{ {1.f, 1.f, -1.f, 1.0f},		{1.f, 1.f, 0.f, 1.0f} }, // 2
									Vertex{ {1.f, -1.f, -1.f, 1.0f},	{1.f, 0.f, 0.f, 1.0f} }, // 3
									Vertex{ {-1.f, -1.f, 1.f, 1.0f},	{0.f, 0.f, 1.f, 1.0f} }, // 4
									Vertex{ {-1.f, 1.f, 1.f, 1.0f},		{0.f, 1.f, 1.f, 1.0f} }, // 5
									Vertex{ { 1.f, 1.f, 1.f, 1.0f},		{1.f, 1.f, 1.f, 1.0f} }, // 6
									Vertex{ { 1.f, -1.f, 1.f, 1.0f},	{1.f, 0.f, 1.f, 1.0f} });  // 7
		
		Array_View<u16>indices_data{};
		indices_data.init(&app_state->arena_assets, 		
		                  0, 1, 2, 0, 2, 3,
		                  4, 6, 5, 4, 7, 6,
		                  4, 5, 1, 4, 1, 0,
		                  3, 2, 6, 3, 6, 7,
		                  1, 5, 6, 1, 6, 2,
		                  4, 0, 3, 4, 3, 7);
		
		data_to_rhi->st_verts		=	vertex_data;
		data_to_rhi->st_indices	=	indices_data;
		data_to_rhi->shader_path = L"../source/shaders/simple.hlsl";
		data_to_rhi->is_new_static = true;
		
		app_state->camera = { .pos = { 3.0f, 0.0f, 3.0f }, .yaw = PI32 + PI32 / 4.0f, .fov = 50.0f };
		
		memory->is_initalized = true;
	}
	
	// Camera
	{
		camera->forward = lib::normalize( lib::Vec3{
		                  .x = cos(camera->yaw) * cos(camera->pitch),
											.y = sin(camera->pitch),
											.z = sin(camera->yaw) * cos(camera->pitch) });
	}

	// Input check
	{
		for (auto& controller : inputs->controllers)
		{
			if(controller.isConnected)
			{
				// Mouse
				{
					if(controller.rotate_start.wasDown)
					{
						constexpr f32 sens = 0.025f;
            camera->yaw		= camera->yaw + sens * controller.mouse.delta_x;
						camera->pitch	= camera->pitch + sens * -controller.mouse.delta_y;
						
						camera->pitch = lib::clamp(camera->pitch, -0.49f * PI32, 0.49f * PI32);
						camera->yaw		= lib::mod_pi(camera->yaw);
					}
					
					camera->fov = camera->fov + -controller.mouse.delta_wheel * 0.1f;
					camera->fov = lib::clamp(camera->fov, 3.0f, 100.0f);
				}
				
				lib::Vec3 cam_rightward = lib::normalize(lib::cross(camera->forward, { 0.0f, 1.0f, 0.0f }));
				
				// Keyboard keys
				if(controller.move_forward.wasDown)
				{
					camera->pos = move_camera(camera->pos, camera->forward);
				}
				if(controller.move_backward.wasDown)
				{
					camera->pos = move_camera(camera->pos, -camera->forward);
				}
				if(controller.move_right.wasDown)
				{
					camera->pos = move_camera(camera->pos, cam_rightward);
				}
				if(controller.move_left.wasDown)
				{
					camera->pos = move_camera(camera->pos, -cam_rightward);
				}
			}
		}
	}
	
	// Modyfing/Creating data for RHI
	{
		lib::Mat4 mat_view = lib::create_look_at( camera->pos, camera->pos + camera->forward, { 0.0f, 1.0f, 0.0f });
		lib::Mat4 mat_trans = lib::create_translate({-0.33f, 0.0f, 0.0f});
		lib::Mat4 mat_rotation = lib::create_rotation_z((f32)window->time_ms / 1000.0f);
		lib::Mat4 mat_projection = lib::create_perspective(lib::deg_to_rad(camera->fov), 
		                                                   (f32)window->width/window->height, 
		                                                   0.1f, 
		                                                   100.0f);
		lib::Mat4 mat_model = mat_trans * mat_rotation;
		
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
			data_to_rhi->cb_draw.world_to_clip = mat_projection * mat_view;
		}
	}
	
	return data_to_rhi;
}