#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"
#include "../source/shaders/common_root_signature.hlsli"

struct PSInput
{
	float4 pos_ndc 	: SV_POSITION;
	float3 pos 			: POSITION;
};

[RootSignature(RootSignatureCommon)]
PSInput VSMain(uint id : SV_VertexID)
{
	PSInput result;
	
	// generate full screen triangle from vertex id
	result.pos_ndc = float4(-1.0 + float((id & 1) << 2), 
													-1.0 + float((id & 2) << 1), 1.0, 1.0);
	result.pos = mul(cb_per_draw.clip_to_world, result.pos_ndc).xyz;
	
	return result;
}

[RootSignature(RootSignatureCommon)]
float4 PSMain(PSInput inp) : SV_TARGET
{
	TextureCube<float4>env_tex = ResourceDescriptorHeap[cb_draw_ids.env_id];
	float3 skybox_color = env_tex.Sample(sam_linear, inp.pos).rgb;
	
	return float4(skybox_color, 1.0f);
}