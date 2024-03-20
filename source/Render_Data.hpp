#pragma once
#include "Shader_And_CPU_Common.h"

struct Vertex
{
	lib::Vec4 position;
	lib::Vec4 color;
};

struct GPU_Data_Static
{
	GPU_Resource verts;
	GPU_Resource indices;
	
	Pipeline default_pipeline;
	
	Constant_Data_Frame cb_frame;
	Constant_Data_Draw cb_draw;
};