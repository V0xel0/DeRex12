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

struct App_State
{
	Alloc_Arena arena_persist;
	Alloc_Arena arena_transient;
	b32 is_transient_initalized;
	
	Camera camera;
};
