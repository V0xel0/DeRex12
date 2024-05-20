#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"

#define RootSignatureBasic \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), " \
"RootConstants(b0, num32BitConstants=2, visibility = SHADER_VISIBILITY_ALL), " \
	"CBV(b1), " \
	"CBV(b2, visibility = SHADER_VISIBILITY_PIXEL)," \
"StaticSampler(" \
"   s0, " \
"   filter = FILTER_ANISOTROPIC, " \
"   maxAnisotropy = 16, " \
"   visibility = SHADER_VISIBILITY_PIXEL" \
")"

SamplerState sam_linear : register(s0);

ConstantBuffer<Draw_Ids>						cb_draw_ids 	: register(b0);
ConstantBuffer<Constant_Data_Draw>	cb_per_draw 	: register(b1);
ConstantBuffer<Constant_Data_Frame>	cb_per_frame 	: register(b2);

struct PSInput
{
	float4 pos_ndc : SV_POSITION;
	float4 color : COLOR;
};

[RootSignature(RootSignatureBasic)]
PSInput VSMain(uint vertex_id : SV_VertexID)
{
	PSInput result;
 
	Buffer<u16> indices_buffer 					= ResourceDescriptorHeap[cb_draw_ids.ind_id];
	StructuredBuffer<Vertex> pos_buffer = ResourceDescriptorHeap[cb_draw_ids.pos_id];
	
	u32 vert_index = indices_buffer[vertex_id];
	float4 pos = float4(pos_buffer[vert_index].position, 1.0f);
	
	const float4x4 obj_to_world = cb_per_draw.obj_to_world;
	const float4x4 obj_to_clip = mul(cb_per_draw.world_to_clip, obj_to_world);
	
	result.pos_ndc = mul(obj_to_clip, pos);
	//result.color = pos_buffer[vert_index].color;
 
	return result;
}

[RootSignature(RootSignatureBasic)]
float4 PSMain(PSInput input) : SV_TARGET
{
	//return input.color;
	return cb_per_frame.light_col;
}