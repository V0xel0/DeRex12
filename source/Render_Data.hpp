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

struct Data_To_RHI
{
	Memory_View st_verts;
	Memory_View st_indices;
	Memory_View st_uvs;
	
	Image_View st_albedo;
	
	const wchar_t* shader_path;
	b32 is_new_static;
	
	Memory_View cb_frame;
	Memory_View cb_draw;
};