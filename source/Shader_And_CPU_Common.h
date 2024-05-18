#pragma once

#ifdef __cplusplus
#include "Math.hpp"
using namespace lib;
#endif

struct Draw_Ids
{
	u32 pos_id;
	u32 ind_id;
};

struct Vertex
{
	Vec4 position;
	Vec4 color;
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