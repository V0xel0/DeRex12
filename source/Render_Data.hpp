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
	Array_View<Vertex> st_verts;
	Array_View<u32> st_indices;
	const wchar_t* shader_path;
	b32 is_new_static;
	
	Constant_Data_Frame cb_frame;
	Constant_Data_Draw cb_draw;
	f64 time_passed_ms;
};