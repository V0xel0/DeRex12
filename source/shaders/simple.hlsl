#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"

#define RootSignatureBasic \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), " \
"RootConstants(b0, num32BitConstants=16, visibility = SHADER_VISIBILITY_ALL), " \
"CBV(b1, visibility = SHADER_VISIBILITY_ALL)," \
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
	float3 pos : POSITION;
	float4 tangent : TANGENT;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD;
};

[RootSignature(RootSignatureBasic)]
PSInput VSMain(uint vertex_id : SV_VertexID)
{
	PSInput result;
 
	Buffer<u16> indices_buffer 							= ResourceDescriptorHeap[cb_draw_ids.ind_id];
	StructuredBuffer<Vertex> pos_buffer 		= ResourceDescriptorHeap[cb_draw_ids.pos_id];
	StructuredBuffer<Attributes>attributes 	= ResourceDescriptorHeap[cb_draw_ids.attr_id];
	
	u32 vert_index = indices_buffer[vertex_id];
	float4 pos = float4(pos_buffer[vert_index].position, 1.0f);
	
	const float4x4 obj_to_world = cb_per_draw.obj_to_world;
	const float4x4 obj_to_clip = mul(cb_per_draw.world_to_clip, obj_to_world);
	
	result.pos_ndc = mul(obj_to_clip, pos);
	result.pos = mul(obj_to_world, pos).xyz;
	
	result.uv = attributes[vert_index].uv.xy;
	result.tangent = attributes[vert_index].tangent;
	result.normal = attributes[vert_index].normal.xyz;
	
	return result;
}

[RootSignature(RootSignatureBasic)]
float4 PSMain(PSInput inp) : SV_TARGET
{
	Texture2D<float4>albedo_tex = ResourceDescriptorHeap[cb_draw_ids.albedo_id];
	Texture2D<float4>normal_tex = ResourceDescriptorHeap[cb_draw_ids.normal_id];
	
	const float3x3 obj_to_world = (float3x3)cb_per_draw.obj_to_world;
	
	float3 normal = normalize(inp.normal);
	// sampling normal from non compressed texture for now
	float3 tangent = normalize(inp.tangent.xyz);
	float3 bitangent = normalize(cross(inp.normal, tangent)) * inp.tangent.w;
	
	float3 normal_t = normalize(normal_tex.Sample(sam_linear, inp.uv).rgb * 2.0f - 1.0f);
	float3x3 tn_basis = mul(obj_to_world, transpose(float3x3(tangent, bitangent, normal)));
	normal_t = normalize(mul(tn_basis, normal_t));
	
	float3 col = pow(normal_t * 0.5f + 0.5f, 2.2);
	//float3 col = albedo_tex.Sample(sam_linear, inp.uv).rgb;
	
	return float4(col, 1.0f);
}