 
struct GradePush
{
    float4 tint;  
    float4 view;  
};
[[vk::push_constant]] ConstantBuffer<GradePush> pc;

[[shader("fragment")]]
float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 p = (uv - 0.5) * float2(pc.view.x, 1.0);
    const float r = length(p) / (0.5 * length(float2(pc.view.x, 1.0)));
    const float v = 1.0 - pc.tint.w * smoothstep(0.45, 1.15, r);
    return float4(pc.tint.rgb * v, 1.0);
}
