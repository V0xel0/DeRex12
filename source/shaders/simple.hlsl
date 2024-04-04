#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"

#define RootSignatureBasic \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
	"CBV(b0), " \
	"CBV(b1, visibility = SHADER_VISIBILITY_PIXEL)"

ConstantBuffer<Constant_Data_Draw>	cb_per_draw 	: register(b0);
ConstantBuffer<Constant_Data_Frame>	cb_per_frame 	: register(b1);

struct PSInput
{
	float4 pos_ndc : SV_POSITION;
	float4 color : COLOR;
};

[RootSignature(RootSignatureBasic)]
PSInput VSMain(in float4 position : POSITION, in float4 color : COLOR)
{
	PSInput result;
 
	const float4x4 obj_to_world = cb_per_draw.obj_to_world;
	const float4x4 obj_to_clip = mul(cb_per_draw.world_to_clip, obj_to_world);
	
	result.pos_ndc = mul(obj_to_clip, position);
	result.color = color;
 
	return result;
}

[RootSignature(RootSignatureBasic)]
float4 PSMain(PSInput input) : SV_TARGET
{
	//return input.color;
	return cb_per_frame.light_col * input.color;
}