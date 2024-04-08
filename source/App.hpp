#pragma once

/* TODOs
 * 
 * 1)	Data pools for GPU & CPU Data
 * 2) Subsystem for rendering?
 * 3) Move GPU related data to new subsystem
 * 		subsytem for creation of data (probably RHI related)
 * 		subsytem for consumption of data
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
	Alloc_Arena arena_assets;
	
	RHI_State rhi;
	Data_To_RHI data_for_rhi;
	
	Camera camera;
};
