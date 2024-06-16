// Disney's reparametrization of alpha = roughness^2.

static const float3 f0_dielectric = 0.04;

float ndf_ggx(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

float shlick_g1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float geo_ggx(float cos_Li, float cos_Lo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Roughness remap from Epic
	return shlick_g1(cos_Li, k) * shlick_g1(cos_Lo, k);
}

float3 fresnel_shlick(float3 F0, float cos_theta)
{
	return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}