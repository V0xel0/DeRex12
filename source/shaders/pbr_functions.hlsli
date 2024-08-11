// Disney's reparametrization of alpha = roughness^2.

float ndf_ggx(float noh, float roughness)
{
	float alpha   = roughness * roughness;
	float alpha_sq = alpha * alpha;
	float denom = (noh * noh) * (alpha_sq - 1.0) + 1.0;
	
	return alpha_sq / (g_PI * denom * denom);
}

// optimized correlated, from "moving frostbite to pbr 3.0" notes
float geo_smith_ggx_correlated(float nol, float nov, float roughness) 
{
	float alpha_g2 = roughness * roughness ;
	float ggx_v = nol * sqrt (( -nov * alpha_g2 + nov ) * nov + alpha_g2 );
	float ggx_l = nov * sqrt (( -nol * alpha_g2 + nol ) * nol + alpha_g2 );
	
	return 0.5 / ( ggx_v + ggx_l );
}

float3 fresnel_shlick(float3 f0, float nox)
{
	return f0 + (1.0 - f0) * pow(1.0 - nox, 5.0);
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