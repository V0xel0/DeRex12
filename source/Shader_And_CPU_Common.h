#pragma once

#ifdef __cplusplus
#include "Math.hpp"
using namespace lib;
#endif

//============================ RHI -> Shader ============================
struct Draw_Ids
{
	u32 pos_id;
	u32 ind_id;
	u32 attr_id;
	u32 albedo_id;
	u32 normal_id;
	u32 rough_id;
};


//============================ Application -> Shader ============================
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
	Vec4 light_pos;
	Vec4 light_col;
	Vec4 view_pos;
};

struct Constant_Data_Draw
{
	Mat4 obj_to_world;
	Mat4 world_to_clip;
	Vec3 albedo;
	f32 pad;
};