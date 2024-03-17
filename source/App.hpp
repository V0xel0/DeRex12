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

struct App_State
{
	Alloc_Arena arena_persist;
	Alloc_Arena arena_assets;
	
	RHI_State rhi;
	GPU_Data_Static gpu_static;
};
