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

inline internal lib::Vec3 move_camera(lib::Vec3 cam_pos, lib::Vec3 dir, f32 speed = 0.2f)
{
	lib::Vec3 out = cam_pos;
	
	return out + dir * speed;
}

Geometry load_geometry_from_gltf(const char* file_path, Alloc_Arena* arena_to_push, Alloc_Arena* arena_temp)
{
	assert(arena_to_push != arena_temp && "Same arenas");
	
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
			
	auto copy_data = [](Alloc_Arena* arena, Memory_View& mem, void* src, u32 size, u32 stride)
										{
									 		mem.data = allocate(arena, size);
											mem.bytes = size;  
											mem.stride = stride; // or stride from bufferView?
											memcpy(mem.data, src, mem.bytes);
										};
			
	// Copy indices
	{
		cgltf_accessor* acc = data->meshes[0].primitives[0].indices;
		auto src_data = (byte *)acc->buffer_view->buffer->data + acc->offset + acc->buffer_view->offset;
		u32 size_bytes = (u32)acc->buffer_view->size; // shouldnt this be subtracted with offset from accesor?
				
		copy_data(arena_to_push, out.indices, src_data, size_bytes, (u32)acc->stride);
	}
			
	// Copy attributes
	Memory_View temp_normals{};
	Memory_View temp_tangents{};
	Memory_View temp_uvs{};
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
				copy_data(arena_to_push, out.positions, src_data, size_bytes, (u32)acc->stride);
			}
			// Rest of attributes copied first to intermediate buffers
			else if (attribute->type == cgltf_attribute_type_normal)
			{
				AlwaysAssert(acc->type == cgltf_type_vec3 && acc->component_type == cgltf_component_type_r_32f);
				copy_data(arena_temp, temp_normals, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_tangent)
			{
				AlwaysAssert(acc->type == cgltf_type_vec4 && acc->component_type == cgltf_component_type_r_32f);
				copy_data(arena_temp, temp_tangents, src_data, size_bytes, (u32)acc->stride);
			}
			else if (attribute->type == cgltf_attribute_type_texcoord)
			{
				AlwaysAssert(acc->type == cgltf_type_vec2 && acc->component_type == cgltf_component_type_r_32f);
				copy_data(arena_temp, temp_uvs, src_data, size_bytes, (u32)acc->stride);
			}
		}
	}
	
	s32 vertex_count = (s32)(out.positions.bytes / out.positions.stride);
	out.attributes.init(arena_to_push, vertex_count);
	
	// Copy rest of attributes to Geometry layout
	for(u64 v_i = 0; v_i < out.attributes.size; ++v_i)
	{
		// casts are "safe" here cause we know the data has this type
		lib::Vec3 normal = *(lib::Vec3*)((byte *)(temp_normals.data) + v_i * temp_normals.stride);
		lib::Vec4 tangent = *(lib::Vec4*)((byte *)(temp_tangents.data) + v_i * temp_tangents.stride);
		lib::Vec2 uv = *(lib::Vec2*)((byte *)(temp_uvs.data) + v_i * temp_uvs.stride);
		
		out.attributes.push( {{ tangent.x, tangent.y, tangent.z, tangent.w },
													{ normal.x, normal.y, normal.z, 0.0f }, 
													{ uv.x, uv.y, 0.0f, 0.0f } });
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
		// Loading geometry
		//TODO: get URI for textures from gltf
		//TODO: temporarily not holding it anywhere
		//TODO: async loading
		Geometry lvl_geo = load_geometry_from_gltf("../assets/meshes/scifihelmet/SciFiHelmet.gltf", &app_state->arena_assets, &app_state->arena_frame);
		
		//TODO: compress and save as .dds - maybe do compression in RHI?
		//TODO: material abstraction that hold indexes to textures
		Image_View lvl_tex_albedo = memory->os_api.read_img(L"../assets/meshes/scifihelmet/SciFiHelmet_BaseColor.png", &app_state->arena_assets, true);
		Image_View lvl_tex_normal = memory->os_api.read_img(L"../assets/meshes/scifihelmet/SciFiHelmet_Normal.png", &app_state->arena_assets, false);
		Image_View lvl_tex_rough = memory->os_api.read_img(L"../assets/meshes/scifihelmet/SciFiHelmet_MetallicRoughness.png", &app_state->arena_assets, false);
		Image_View lvl_tex_enviro = memory->os_api.read_img(L"../assets/cubemap_test.png", &app_state->arena_assets, true);
		
		// Sending static geometric data to RHI
		data_to_rhi->st_geo = lvl_geo;
		// Sending static textures
		data_to_rhi->st_albedo = lvl_tex_albedo;
		data_to_rhi->st_normal = lvl_tex_normal;
		data_to_rhi->st_roughness = lvl_tex_rough;
		data_to_rhi->st_enviro = lvl_tex_enviro;
		
		data_to_rhi->shader_path = L"../source/shaders/simple.hlsl";
		
		app_state->camera = { .pos = { 0.0f, 1.0f, 20.0f }, .yaw = -PI32 / 2.0f , .fov = 50.0f };
		
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
				lib::Vec3 cam_upward = lib::normalize(lib::cross(camera->forward, cam_rightward));
				
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
				if (controller.action1.wasDown)
				{
					camera->pos = move_camera(camera->pos, cam_upward);
				}
				if (controller.action2.wasDown)
				{
					camera->pos = move_camera(camera->pos, -cam_upward);
				}
			}
		}
	}
	
	// Modyfing/Creating data for RHI
	{
		lib::Mat4 mat_view = lib::create_look_at( camera->pos, camera->pos + camera->forward, { 0.0f, 1.0f, 0.0f });
		lib::Mat4 mat_scale = lib::create_scale(4.0f);
		lib::Mat4 mat_trans = lib::create_translate({0.0f, 0.0f, 0.0f});
		lib::Mat4 mat_rotation = lib::create_rotation_y((f32)window->time_ms / 1000.0f);
		lib::Mat4 mat_projection = lib::create_perspective(lib::deg_to_rad(camera->fov), 
		                                                   (f32)window->width/window->height, 
		                                                   0.1f, 
		                                                   100.0f);
		lib::Mat4 mat_model = mat_trans  * mat_scale;
		
		// Pushing data
		{
			auto* frame_consts	=	push_type<Constant_Data_Frame>(&app_state->arena_frame);
			auto* draw_consts		=	push_type<Constant_Data_Draw>(&app_state->arena_frame);
			
			// Frame constants
			{
				frame_consts->lights[0].pos = { 70, 50, 10, 0.0f};
				frame_consts->lights[1].pos = {	-90, -40, -10, 0.0f};
				frame_consts->lights[2].pos = {	-5, -10, -15, 0.0f};
				frame_consts->lights[3].pos = {	13, 15, 15, 0.0f};
				frame_consts->lights[0].radiance = { 0.9f, 0.9f, 0.9f, 200.0f };
				frame_consts->lights[1].radiance = { 0.9f, 0.9f, 0.9f, 240.0f };
				frame_consts->lights[2].radiance = { 0.9f, 0.9f, 0.9f, 5.0f };
				frame_consts->lights[3].radiance = { 0.9f, 0.9f, 0.9f, 40.0f };
				
				frame_consts->view_pos = { lib::Vec3{camera->pos}, 1.0f };
			}
			
			// Draw constants
			{
				draw_consts->obj_to_world = mat_model;
				draw_consts->world_to_clip = mat_projection * mat_view;
			
				data_to_rhi->cb_frame = { .data = frame_consts, .bytes = sizeof(*frame_consts) };
				data_to_rhi->cb_draw  = { .data = draw_consts, .bytes = sizeof(*draw_consts)  };
			}
		}
	}
	
	return data_to_rhi;
}