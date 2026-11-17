 
[[vk::binding(0, 1)]] Texture2D globalTextureImages[];
[[vk::binding(1, 1)]] StructuredBuffer<uint> globalTextureSamplerIndices;
[[vk::binding(0, 2)]] SamplerState globalTextureSamplers[];

static const uint FLAG_PLANE = 128u;
static const uint FLAG_LIT = 1u;
static const uint FLAG_STRETCH = 2u;
static const uint FLAG_SOFT = 4u;
static const uint FLAG_NORMAL = 8u;
static const uint CTL_HAS_DEPTH = 1u;
static const uint CTL_SRGB_TARGET = 2u;
static const uint CTL_SOFT = 4u;
static const uint CTL_CHEAP = 8u;  

struct ParticlePush
{
    uint4 ctl;         
    float4 sunDir;     
    float4 sunColour;  
    float4 sky;        
    float4 fogColour;  
    float4 view;       
    float4 misc;       
};
[[vk::push_constant]] ConstantBuffer<ParticlePush> pc;

struct ParticleVaryings
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 colour : COLOR0;
    nointerpolation uint tex : TEXCOORD1;
    nointerpolation uint frame : TEXCOORD2;
    nointerpolation float4 emission : TEXCOORD3;  
    nointerpolation float3 axisX : TEXCOORD4;
    nointerpolation float3 axisY : TEXCOORD5;
    nointerpolation float3 back : TEXCOORD6;
    float depth : TEXCOORD7;
    nointerpolation float4 fog : TEXCOORD8;  
};

float4 unpackUnorm(uint v)
{
    return float4(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u), float(v >> 24)) / 255.0;
}

float linearDepth(float d) { return d <= 1e-7 ? 1e30 : pc.view.y / (d + pc.view.x); }

 
float4 sampleAtlas(uint index, float2 uv, float2 dx, float2 dy)
{
    Texture2D image = globalTextureImages[NonUniformResourceIndex(index)];
    const uint samplerIndex = globalTextureSamplerIndices[NonUniformResourceIndex(index)];
    return image.SampleGrad(globalTextureSamplers[NonUniformResourceIndex(samplerIndex)], uv, dx, dy);
}
