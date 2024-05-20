#pragma once

/* TODOs
 * 
 * 1)	Data pools for GPU & CPU Data
 * 2) Transient state
 * 
 * 
 * */

struct Camera
{
	lib::Vec3 pos;
	lib::Vec3 forward;
	f32 pitch;
	f32 yaw;
	f32 fov;
};

struct Geometry
{
	Memory_View indices;
	Memory_View positions;
	Memory_View uvs;
	Memory_View normals;
	Memory_View tangents;
	Memory_View colors;
};

struct App_State
{
	Alloc_Arena arena_persist;
	Alloc_Arena arena_transient;
	
	Alloc_Arena arena_frame;
	Alloc_Arena arena_assets;
	b32 is_new_level;
	
	Camera camera;
};
