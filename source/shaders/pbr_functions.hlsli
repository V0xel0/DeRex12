// Disney's reparametrization of alpha = roughness^2.

float ndf_ggx(float noh, float alpha)
{
	float alpha_2 = alpha * alpha;
	float denom = (noh * noh) * (alpha_2 - 1.0) + 1.0;
	
	return alpha_2 / (g_PI * denom * denom);
}

// optimized correlated, from "moving frostbite to pbr 3.0" notes
float geo_smith_ggx_correlated(float nol, float nov, float alpha) 
{
	float alpha_2 = alpha * alpha;
	float ggx_v = nol * sqrt (( -nov * alpha_2 + nov ) * nov + alpha_2 );
	float ggx_l = nov * sqrt (( -nol * alpha_2 + nol ) * nol + alpha_2 );
	
	return 0.5 / ( ggx_v + ggx_l );
}

float3 fresnel_shlick(float3 f0, float nox)
{
	return f0 + (1.0 - f0) * pow(1.0 - nox, 5.0);
}

float3 fresnel_shlick_rough(float3 f0, float nox, float roughness) 
{
	float3 fr = max( 1.0 - roughness, f0) - f0;
	return f0 + fr * pow(1.0 - nox, 5.0);
}

// from: https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
float3 env_brdf_approx(float3 f0, float nov, float roughness)
{
	float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
	float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * nov)) * r.x + r.y;
	float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
	
	return f0 * AB.x + AB.y;
}

// from UE4, heurisitc that maps roughness to mip level
float mip_from_roughness(float roughness, float max_mip)
{
	const float rough_mip_scale = 1.2;
	const float roughest_mip = 1.0;
	const float level_from_1x1 = roughest_mip - rough_mip_scale * log2(roughness);
	
	return max_mip - 1 - level_from_1x1;
}

// ------------------------------ OLD ----------------------------------------------------------------------
float3 get_view_reflected_normal(float3 n, float3 v)
{
	float nov = dot(n, v);
	n += (2.0 * saturate(-nov)) * v;
	return n;
}

float shlick_g1(float nox, float k)
{
	return nox / (nox * (1.0 - k) + k);
}

float geo_smith_schlick(float nol, float nov, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Roughness remap from Epic
	
	return shlick_g1(nol, k) * shlick_g1(nov, k);
}