 
 
#include "include/wz_particles_common.slang"

struct VSIn
{
    [[vk::location(0)]] float4 a : A;    
    [[vk::location(1)]] uint4 b : B;     
    [[vk::location(2)]] float4 c : C;    
    [[vk::location(3)]] float4 vp0 : K0;  
    [[vk::location(4)]] float4 vp1 : K1;
    [[vk::location(5)]] float4 vp2 : K2;
    [[vk::location(6)]] float4 vp3 : K3;
    [[vk::location(7)]] float4 cam : K4;    
    [[vk::location(8)]] float4 right : K5;  
    [[vk::location(9)]] float4 up : K6;     
    [[vk::location(10)]] float4 fog : K7;   
    uint vertex : SV_VertexID;
};

float4 project(VSIn v, float3 p) { return v.vp0 * p.x + v.vp1 * p.y + v.vp2 * p.z + v.vp3; }

 
float sceneDepthAt(float4 clip)
{
    Texture2D depthImage = globalTextureImages[NonUniformResourceIndex(pc.ctl.x)];
    uint w, h;
    depthImage.GetDimensions(w, h);
    const float2 ndc = clip.xy / clip.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 * sign(pc.view.z));
    if (any(uv < 0.0) || any(uv > 1.0))
        return 1e30;  
    const float d = depthImage.Load(int3(min(int2(uv * float2(w, h)), int2(w - 1, h - 1)), 0)).r;
    return linearDepth(d);
}

[[shader("vertex")]]
ParticleVaryings main(VSIn v)
{
    ParticleVaryings o;
    const float2 corner = float2((v.vertex & 1u) != 0u ? 1.0 : -1.0, (v.vertex & 2u) != 0u ? 1.0 : -1.0);
    const float3 centre = v.a.xyz;
    float r = v.a.w;
    const uint flags = v.b.z >> 24;
    const float3 fwd = cross(v.up.xyz, v.right.xyz);
    const float3 toEye = v.cam.xyz - centre;
    const float dist = max(length(toEye), 1e-3);
    const float depth = dot(-toEye, fwd);

     
    const float screen = r * abs(pc.view.z) / max(depth, 0.05);
    if (screen > pc.misc.x)
        r *= pc.misc.x / screen;

    float3 world;
    float3 axisX, axisY;
    if ((flags & FLAG_PLANE) != 0u)
    {
        const float sn = sin(v.c.x), co = cos(v.c.x);
        axisX = float3(co, 0, sn);
        axisY = float3(-sn, 0, co);
        world = centre + (axisX * corner.x + axisY * corner.y) * r;
    }
    else if ((flags & FLAG_STRETCH) != 0u)
    {
        const float3 head = centre, tail = centre + v.c.xyz;
        const float3 along = normalize(head - tail + float3(0, 1e-5, 0));
        axisY = normalize(cross(along, toEye) + float3(1e-6, 0, 0));
        axisX = along;
        const float t = corner.x * 0.5 + 0.5;
        world = lerp(tail - along * r, head + along * r, t) + axisY * (corner.y * r);
    }
    else
    {
        float rotation = v.c.x;
        if ((flags & FLAG_NORMAL) != 0u && (flags & FLAG_LIT) == 0u)
            rotation = atan2(dot(v.c.xyz, v.up.xyz), dot(v.c.xyz, v.right.xyz));
        const float s = sin(rotation), co = cos(rotation);
        axisX = v.right.xyz * co + v.up.xyz * s;
        axisY = -v.right.xyz * s + v.up.xyz * co;
        world = centre + (axisX * corner.x + axisY * corner.y) * r;
    }
    o.position = project(v, world);

     
     
    if ((pc.ctl.y & CTL_HAS_DEPTH) != 0u && (flags & FLAG_STRETCH) == 0u)
    {
        const float front = depth - r;
        bool hidden = sceneDepthAt(project(v, centre)) < front;
        if (hidden)
        {
            const float3 ex = axisX * r, ey = axisY * r;
            hidden = sceneDepthAt(project(v, centre + ex + ey)) < front && sceneDepthAt(project(v, centre - ex + ey)) < front &&
                     sceneDepthAt(project(v, centre + ex - ey)) < front && sceneDepthAt(project(v, centre - ex - ey)) < front;
        }
        if (hidden)
            o.position = float4(0, 0, -2, 1);  
    }

    o.uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);
    o.colour = unpackUnorm(v.b.x);
    o.tex = v.b.y;
    o.frame = v.b.z;
    if ((flags & FLAG_NORMAL) != 0u && (flags & FLAG_LIT) == 0u)
    {
        const uint base = abs(dot(normalize(v.c.xyz), toEye / dist)) > 0.7 ? 0u : 32u;
        const uint frame = base + (v.b.z & 31u);
        o.frame = (v.b.z & 0xFF000000u) | frame | (frame << 8);
    }
    o.emission = float4(unpackUnorm(v.b.w).rgb * v.c.w, r * (float(v.b.w >> 24) / 50.0) * asfloat(pc.ctl.z));
    o.axisX = axisX;
    o.axisY = axisY;
    o.back = toEye / dist;
    o.depth = dot(world - v.cam.xyz, fwd);

     
    const float3 dir = -toEye / dist;
    const float a = v.fog.x * exp(-(v.cam.y - v.fog.z) / v.fog.y);
    const float k = dir.y * dist / v.fog.y;
    const float shape = abs(k) > 1e-4 ? (1.0 - exp(-k)) / k : 1.0;
    const float glow = pow(saturate(dot(dir, pc.sunDir.xyz)), 8.0) * pc.fogColour.w;
    o.fog = float4(glow, v.c.y, 0, min(1.0 - exp(-a * dist * shape), v.fog.w));
    return o;
}
