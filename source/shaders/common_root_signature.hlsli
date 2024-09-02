#define RootSignatureCommon \
"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), " \
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