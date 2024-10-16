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

static const float 	g_PI 						= 3.141592;
static const float 	g_epsilon 			= 0.00001;
static const float3 g_f0_dielectric = 0.04; //TODO: reparametrization, see filament & frostbite

static const float exposure  = 4.4; //TODO: make it from app, later automatic
static const float white_point = 1.0; //TODO: make it from app, later automatic

#include "../source/shaders/pbr_functions.hlsli"
#include "../source/shaders/tonemappers.hlsli"

[RootSignature(RootSignatureCommon)]
float4 PSMain(PSInput inp) : SV_TARGET
{
	Texture2D<float4>albedo_tex = ResourceDescriptorHeap[cb_draw_ids.albedo_id];
	Texture2D<float4>normal_tex = ResourceDescriptorHeap[cb_draw_ids.normal_id];
	Texture2D<float4>rough_tex 	= ResourceDescriptorHeap[cb_draw_ids.rough_id];
	Texture2D<float4>ao_tex 		= ResourceDescriptorHeap[cb_draw_ids.ao_id];
	
	TextureCube<float4>env_tex 			= ResourceDescriptorHeap[cb_draw_ids.env_id];
	TextureCube<float4>env_irr_tex 	= ResourceDescriptorHeap[cb_draw_ids.env_irr_id];
	
	const float3x3 obj_to_world = (float3x3)cb_per_draw.obj_to_world;
	const float2 met_rough 			= rough_tex.Sample(sam_linear, inp.uv).gb;
	const float3 ao 						= ao_tex.Sample(sam_linear, inp.uv).rgb;
	
	const float3 albedo 				= albedo_tex.Sample(sam_linear, inp.uv).rgb;
	const float metallic 				= met_rough.y;
	const float roughness 			= max(0.045, met_rough.x);
	const float roughness_sq 		= max(0.002025, roughness * roughness);
	
	float3 normal 		= normalize(inp.normal);
	float3 tangent 		= normalize(inp.tangent.xyz);
	float3 bitangent	= normalize(cross(inp.normal, tangent)) * inp.tangent.w;
	float3x3 tn_basis = mul(obj_to_world, transpose(float3x3(tangent, bitangent, normal)));
	
	float3 normal_t = normalize(normal_tex.Sample(sam_linear, inp.uv).rgb * 2.0 - 1.0);
	normal_t 				= normalize(mul(tn_basis, normal_t));
	
	float3 view_dir	= normalize(cb_per_frame.view_pos.xyz - inp.pos);
	float nov = abs(dot(normal_t, view_dir)) + g_epsilon;
	float3 f0 = lerp(g_f0_dielectric, albedo, metallic);
	
	float3 direct_radiance = 0.0;
	//float3 debug = 0.0;
	for(uint i = 0; i < 2; ++i)
	{
		//TODO: point light asssumed
		float3 light_i = normalize(cb_per_frame.lights[i].pos.xyz - inp.pos ); 
		float distance = length(cb_per_frame.lights[i].pos.xyz - inp.pos);
		float attenuation = 1.0 / (distance);
		float3 radiance = cb_per_frame.lights[i].radiance.xyz * cb_per_frame.lights[i].radiance.w * attenuation;
		
		float3 halfway = normalize(light_i + view_dir);
		float nol = saturate(dot(normal_t, light_i));
		float noh = saturate(dot(normal_t, halfway));
		float loh = saturate(dot(light_i, halfway));
		
		float3	F 	= fresnel_shlick(f0, loh);
		float		D 	= ndf_ggx(noh, roughness_sq);
		float		V 	= geo_smith_ggx_correlated(nol, nov, roughness_sq);
		float3 kd 	= lerp(1.0 - F, 0.0, metallic);
		
		float3 diffuse 	= kd * albedo;
		float3 specular = F*(D*V);
		
		//debug += float3(nol,nol,nol);
		direct_radiance += (diffuse + specular) * radiance * nol;
	}
	//float3 col = pow(normal_t * 0.5f + 0.5f, 2.2);
	
	float3 indirect_radiance = 0.0;
	{
		u32 count_mips = 0;
		{
			u32 width, height;
			env_tex.GetDimensions(0, width, height, count_mips);
		}
		
		float3 diffuse_irradiance = env_irr_tex.Sample(sam_linear, normal_t).rgb;
		float3 F 									= fresnel_shlick_rough(f0, nov, roughness);
		float3 kd 								= lerp(1.0 - F, 0.0, metallic);
		
		float3 diffuse_ibl = kd * albedo * diffuse_irradiance;
		
		float3 r_vec = reflect(-view_dir, normal_t);
		float mip = mip_from_roughness(roughness, count_mips);
		//float mip2 = roughness * count_mips;
		
		float3 specular_irradiance = env_tex.SampleLevel(sam_linear, r_vec, mip).rgb;
		
		float3 specular_ibl = specular_irradiance * env_brdf_approx(f0, nov, roughness);
		
		indirect_radiance = specular_ibl + diffuse_ibl;
	}
	//TODO: ambient occlusion term should be different for specular and diffuse and also
	// 			it should only affect indirect light (see: filament)
	float3 output_radiance = (indirect_radiance) * ao;
	
	float3 out_color = tonemap_khronos(output_radiance * exposure);
	
	return float4(out_color, 1.0f);
}