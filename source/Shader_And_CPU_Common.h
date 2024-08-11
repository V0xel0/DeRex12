#pragma once

#ifdef __cplusplus
#include "Math.hpp"
using namespace lib;
#endif

//============================ RHI -> Shader ============================
struct Draw_Ids
{
	u32 pos_id;
	u32 attr_id;
	u32 albedo_id;
	u32 normal_id;
	u32 rough_id;
};


//============================ Application -> Shader ============================

static const u32 g_count_lights 	= 4; //TODO: just for a while

struct Vertex
{
	Vec3 position;
};

// because of Vec4 in C++ it is 16bytes aligned so have to extend all to Vec4 for now in order to
// have proper alignment with hlsl
struct Attributes
{
	Vec4 tangent;
	Vec4 normal;
	Vec4 uv;
};

struct Constant_Data_Frame
{
	struct 
	{
		Vec4 pos; // w is light type
		Vec4 radiance; // w is power
	} lights[g_count_lights];
	Vec4 view_pos;
};

struct Constant_Data_Draw
{
	Mat4 obj_to_world;
	Mat4 world_to_clip;
};