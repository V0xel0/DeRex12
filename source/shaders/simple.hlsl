#include "../source/shaders/aliases.hlsli"
#include "../source/Shader_And_CPU_Common.h"

ConstantBuffer<Constant_Data_Draw>	cb_per_draw 	: register(b0);
ConstantBuffer<Constant_Data_Frame>	cb_per_frame 	: register(b1);

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PSInput VSMain(in float4 position : POSITION, in float4 color : COLOR)
{
	PSInput result;
 
	result.position = position;
	result.color = color;
 
	return result;
}
 
float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}