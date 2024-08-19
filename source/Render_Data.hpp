#pragma once
#include "Shader_And_CPU_Common.h"

//TODO: Only static (sent once) data for now
//TODO: Pass generational handles, instead of direct views

struct Game_Window
{
	void* handle;
	f64 time_ms;
	u32 width;
	u32 height;
	b32 is_closed;
};

struct Geometry
{
	Memory_View indices;
	Memory_View positions;
	Array_View<Attributes> attributes; // this can be Memory_View when passed to RHI
};

struct Data_To_RHI
{
	Geometry st_geo;
	
	Image_View st_albedo;
	Image_View st_normal;
	Image_View st_roughness;
	Image_View st_ao;
	
	const wchar_t* shader_path;
	b32 is_new_static;
	
	Memory_View cb_frame;
	Memory_View cb_draw;
};