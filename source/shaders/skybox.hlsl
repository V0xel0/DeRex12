#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"
#include "../source/shaders/common_root_signature.hlsli"


struct PSInput
{
	float4 pos_ndc 	: SV_POSITION;
	float3 pos 			: POSITION;
	float4 tangent 	: TANGENT;
	float3 normal 	: NORMAL;
	float2 uv 			: TEXCOORD;
};

[RootSignature(RootSignatureCommon)]
PSInput VSMain(uint vertex_id : SV_VertexID)
{
	PSInput result;
 
	StructuredBuffer<Vertex> pos_buffer 		= ResourceDescriptorHeap[cb_draw_ids.pos_id];
	StructuredBuffer<Attributes>attributes 	= ResourceDescriptorHeap[cb_draw_ids.attr_id];
	
	float4 pos = float4(pos_buffer[vertex_id].position, 1.0f);
	
	const float4x4 obj_to_world = cb_per_draw.obj_to_world;
	const float4x4 obj_to_clip = mul(cb_per_draw.world_to_clip, obj_to_world);
	
	result.pos_ndc 	= mul(obj_to_clip, pos);
	result.pos 			= mul(obj_to_world, pos).xyz;
	result.uv 			= attributes[vertex_id].uv.xy;
	result.tangent 	= attributes[vertex_id].tangent;
	result.normal		= attributes[vertex_id].normal.xyz;
	
	return result;
}



[RootSignature(RootSignatureCommon)]
float4 PSMain(PSInput inp) : SV_TARGET
{
	
	return float4(out_color, 1.0f);
}