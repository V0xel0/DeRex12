#pragma once

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
};