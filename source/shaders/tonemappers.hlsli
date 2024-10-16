// https://www.khronos.org/news/press/khronos-pbr-neutral-tone-mapper-released-for-true-to-life-color-rendering-of-3d-products
// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl
float3 tonemap_khronos(float3 color )
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) return color;

	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return lerp(color, newPeak * float3(1, 1, 1), g);
}

static const float3x3 ACESInputMat =
{
{0.59719, 0.35458, 0.04823},
{0.07600, 0.90834, 0.01566},
{0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
{ 1.60475, -0.53108, -0.07367},
{-0.10208,  1.10813, -0.00605},
{-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
	float3 a = v * (v + 0.0245786f) - 0.000090537f;
	float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
	return a / b;
}

// ACES filmic tone map approximation by Stephen Hill (@self_shadow)
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
float3 tonemap_ACES(float3 color)
{
	color = mul(ACESInputMat, color);
	color = RRTAndODTFit(color);
	color = mul(ACESOutputMat, color);
	color = saturate(color);
	return color;
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilm(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

//TODO: Make it for reinhardt
//	float3 out_color = output_radiance * exposure;
//	float luminance = dot(out_color, float3(0.2126, 0.7152, 0.0722));
//	float luminance_mapped = (luminance * (1.0 + luminance/(white_point*white_point))) / (1.0 + luminance);
//	out_color = (luminance_mapped / luminance) * out_color;