 
[[vk::binding(0, 1)]] Texture2D globalTextureImages[];

struct HazePush
{
    uint4 idx;       
    float4 proj;     
    float4 fog;      
    float4 cam;      
    float4 fwd;      
    float4 haze;     
    float4 sunDir;   
    float4 sunColor;
};
[[vk::push_constant]] ConstantBuffer<HazePush> pc;

[[shader("fragment")]]
float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    if (pc.idx.y == 0)
        discard;
    Texture2D depthImage = globalTextureImages[NonUniformResourceIndex(pc.idx.x)];
    uint w, h;
    depthImage.GetDimensions(w, h);
    const float d = depthImage.Load(int3(min(int2(uv * float2(w, h)), int2(w - 1, h - 1)), 0)).r;

    const float3 fwd = normalize(pc.fwd.xyz);
    float3 right = cross(fwd, float3(0, 1, 0));
    right = length(right) > 1e-4 ? normalize(right) : float3(1, 0, 0);
    const float3 up = cross(right, fwd);
    const float tanY = 1.0 / max(abs(pc.proj.z), 1e-4);
    const float tanX = tanY * pc.proj.w;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float3 rayView = fwd + right * (ndc.x * tanX) + up * (ndc.y * tanY);
    const float3 dir = normalize(rayView);

    const bool sky = d <= 1e-7;
    float dist = sky ? pc.fwd.w : (pc.proj.y / (d + pc.proj.x)) * length(rayView);
    dist = min(abs(dist), pc.fwd.w);

    const float a = pc.fog.x * exp(-(pc.cam.y - pc.fog.z) / pc.fog.y);
    const float k = dir.y * dist / pc.fog.y;
    const float shape = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0;
    float amount = 1.0 - exp(-a * dist * shape);
    amount = min(amount, pc.fog.w) * (sky ? 0.75 : 1.0);

    const float glow = pow(saturate(dot(dir, normalize(pc.sunDir.xyz))), 8.0) * pc.cam.w;
    const float3 colour = pc.haze.rgb + pc.sunColor.rgb * glow;
    return float4(colour * amount, amount);
}
