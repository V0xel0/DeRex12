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

inline constexpr u64 frame_max_size = MiB(128);
inline constexpr u64 assets_max_size = GiB(1);

inline internal lib::Vec3 move_camera(lib::Vec3 cam_pos, lib::Vec3 dir, f32 speed = 0.15f)
{
	lib::Vec3 out = cam_pos;
	
	return out + dir * speed;
}

Geometry load_geometry_from_gltf(const char* file_path, Alloc_Arena* arena_to_save, Alloc_Arena* arena_temp)
{
	assert(arena_to_save != arena_temp && "Same arenas");
	
	Geometry out{};
			
	arena_start_temp(arena_temp);
	auto d = defer([&] { arena_end_temp(arena_temp); });
			
	cgltf_options options = {.memory = {
																		 .alloc_func = &arena_alloc_for_lib,
																		 .free_func = &arena_reset_for_lib, 
																		 .user_data = arena_temp } };
	cgltf_data* data = nullptr;
		
	cgltf_result result = cgltf_parse_file(&options, file_path, &data);
	assert(result == cgltf_result_success);
	//TODO: Load ourselves based on URIs from cgltf -> dont call load_buffers
	result = cgltf_load_buffers(&options, data, file_path);
	assert(result == cgltf_result_success);
			
	auto copy_data = [arena_to_save](Memory_View& mem, void* src, u32 size, u32 stride)
										{
											mem.data = allocate(arena_to_save, size);
											mem.bytes = size;  
											mem.stride = stride; // or stride from bufferView?
											memcpy(mem.data, src, mem.bytes);
										};
			
	// Copy indices
	{
		cgltf_accessor* acc = data->meshes[0].primitives[0].indices;
		auto src_data = (byte *)acc->buffer_view->buffer->data + acc->offset + acc->buffer_view->offset;
		u32 size_bytes = (u32)acc->buffer_view->size; // shouldnt this be subtracted with offset from accesor?
				
		copy_data(out.indices, src_data, size_bytes, (u32)acc->stride);
	}
			
	// Copy attributes
	{
		u32 count_attributes = (u32)data->meshes[0].primitives[0].attributes_count;
		for (u32 attr_i = 0; attr_i < count_attributes; ++attr_i)
		{
			cgltf_attribute* attribute = &data->meshes[0].primitives[0].attributes[attr_i];
			cgltf_accessor* acc = attribute->data;
			auto src_data = (byte *)acc->buffer_view->buffer->data + acc->offset + acc->buffer_view->offset;
			u32 size_bytes = (u32)acc->buffer_view->size;
					
			if (attribute->type == cgltf_attribute_type_position)
			{
				copy_data(out.positions, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_normal)
			{
				copy_data(out.normals, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_tangent)
			{
				copy_data(out.tangents, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_texcoord)
			{
				copy_data(out.uvs, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_color)
			{
				copy_data(out.colors, src_data, size_bytes, (u32)acc->stride);
			}
		}
	}
			
	return out;
}

extern "C" Data_To_RHI* app_full_update(Game_Memory *memory, Game_Window *window, Game_Input *inputs)
{
	App_State* app_state = (App_State*)memory->permanent_storage;
	auto* camera = &app_state->camera;
	arena_reset(&app_state->arena_frame); // reset all previous frame
	
	if (!memory->is_initalized)
	{
		app_state->arena_persist.max_size 		= memory->size_permanent_storage - sizeof(App_State);
		app_state->arena_persist.base 				= (byte*)memory->permanent_storage + sizeof(App_State);
		app_state->arena_transient.max_size 	= memory->size_transient_storage;
		app_state->arena_transient.base 			= (byte*)memory->transient_storage;
		
		app_state->arena_frame = arena_from_allocator(&app_state->arena_transient, frame_max_size);
		app_state->arena_assets = arena_from_allocator(&app_state->arena_transient, assets_max_size);
			
		memory->is_initalized = true;
	}
	
	auto* data_to_rhi = push_type<Data_To_RHI>(&app_state->arena_frame);
	
	if (!app_state->is_new_level)
	{
		// Loading mesh
		//TODO: temporarily not holding it anywhere
		Geometry level_geo = load_geometry_from_gltf("../assets/meshes/avocado/Avocado.gltf", &app_state->arena_assets, &app_state->arena_frame);
		Image_View level_tex_albedo = memory->os_api.read_img(L"../assets/meshes/avocado/Avocado_baseColor.png", &app_state->arena_assets, true);
		
		// Sending mesh geometric data to RHI
		data_to_rhi->st_verts = level_geo.positions;
		data_to_rhi->st_indices = level_geo.indices;
		data_to_rhi->st_uvs = level_geo.uvs;
		data_to_rhi->st_albedo = level_tex_albedo;
		data_to_rhi->shader_path = L"../source/shaders/simple.hlsl";
		
		app_state->camera = { .pos = { 3.0f, 0.0f, 3.0f }, .yaw = PI32 + PI32 / 4.0f, .fov = 50.0f };
		
		app_state->is_new_level = true;
		data_to_rhi->is_new_static = app_state->is_new_level;
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
			if (controller.isConnected)
			{
				// Mouse
				{
					if (controller.rotate_start.wasDown)
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
				if (controller.move_forward.wasDown)
				{
					camera->pos = move_camera(camera->pos, camera->forward);
				}
				if (controller.move_backward.wasDown)
				{
					camera->pos = move_camera(camera->pos, -camera->forward);
				}
				if (controller.move_right.wasDown)
				{
					camera->pos = move_camera(camera->pos, cam_rightward);
				}
				if (controller.move_left.wasDown)
				{
					camera->pos = move_camera(camera->pos, -cam_rightward);
				}
			}
		}
	}
	
	// Modyfing/Creating data for RHI
	{
		lib::Mat4 mat_view = lib::create_look_at( camera->pos, camera->pos + camera->forward, { 0.0f, 1.0f, 0.0f });
		lib::Mat4 mat_scale = lib::create_scale(70.0f);
		lib::Mat4 mat_trans = lib::create_translate({-0.33f, 0.0f, 0.0f});
		lib::Mat4 mat_rotation = lib::create_rotation_z((f32)window->time_ms / 1000.0f);
		lib::Mat4 mat_projection = lib::create_perspective(lib::deg_to_rad(camera->fov), 
		                                                   (f32)window->width/window->height, 
		                                                   0.1f, 
		                                                   100.0f);
		lib::Mat4 mat_model = mat_trans * mat_rotation  * mat_scale;
		
		f32 pulse = 1;
		
		// Pushing data
		{
			auto* frame_consts =	push_type<Constant_Data_Frame>(&app_state->arena_frame);
			auto* draw_consts =		push_type<Constant_Data_Draw>(&app_state->arena_frame);
			
			// Frame constants
			frame_consts->light_pos = lib::normalize(lib::Vec4 { -6.0f, 1.0f, -6.0f, 1.0f });
			frame_consts->light_col = { pulse, pulse, pulse, 1.0f };
			// Draw constants
			draw_consts->obj_to_world = mat_model;
			draw_consts->world_to_clip = mat_projection * mat_view;
			
			data_to_rhi->cb_frame = { .data = frame_consts, .bytes = sizeof(*frame_consts) };
			data_to_rhi->cb_draw  = { .data = draw_consts, .bytes = sizeof(*draw_consts)  };
		}
	}
	
	return data_to_rhi;
}