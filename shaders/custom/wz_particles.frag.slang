 
 
 
#include "include/wz_particles_common.slang"

float2 cellUv(uint frame, uint grid, float2 uv)
{
    const float2 cell = float2(float(frame % grid), float(frame / grid));
    return (cell + uv) / float(grid);
}

[[shader("fragment")]]
float4 main(ParticleVaryings i) : SV_Target0
{
    const uint flags = i.frame >> 24;
    const uint grid = 1u << ((flags >> 4) & 7u);
    const uint f0 = i.frame & 255u, f1 = (i.frame >> 8) & 255u;
    const float blend = float((i.frame >> 16) & 255u) / 255.0;
    const uint colourTex = i.tex & 0xFFFFu, normalTex = i.tex >> 16;
    const bool cheap = (pc.ctl.y & CTL_CHEAP) != 0u;
    const float2 dx = ddx(i.uv) / float(grid), dy = ddy(i.uv) / float(grid);

     
    float fade = 1.0;
    if ((pc.ctl.y & CTL_HAS_DEPTH) != 0u)
    {
        Texture2D depthImage = globalTextureImages[NonUniformResourceIndex(pc.ctl.x)];
        uint w, h;
        depthImage.GetDimensions(w, h);
        const float2 suv = i.position.xy * pc.misc.yz;
        const float scene = linearDepth(depthImage.Load(int3(min(int2(suv * float2(w, h)), int2(w - 1, h - 1)), 0)).r);
        const float gap = scene - i.depth;
        const bool soft = (flags & FLAG_SOFT) != 0u && (pc.ctl.y & CTL_SOFT) != 0u && i.emission.w > 1e-3;
        fade = soft ? saturate(gap / i.emission.w) : (gap > 0.0 ? 1.0 : 0.0);
    }
    fade *= saturate(i.depth / max(i.emission.w * 2.0, 0.5));  
    if (fade <= 0.002)
        discard;

    float4 tex = sampleAtlas(colourTex, cellUv(f0, grid, i.uv), dx, dy);
    if (blend > 0.004 && !cheap)
        tex = lerp(tex, sampleAtlas(colourTex, cellUv(f1, grid, i.uv), dx, dy), blend);
    const float alpha = tex.a * i.colour.a * fade;
    const bool lit = (flags & FLAG_LIT) != 0u;
    const float3 emission = (lit ? float3(1, 1, 1) : tex.rgb) * i.emission.rgb * tex.a * fade;
    if (alpha < 0.002 && dot(emission, float3(1, 1, 1)) < 0.003)
        discard;

     
    float3 n = float3(0, 0, 1);
    float thickness = tex.a;
    if (lit && !cheap)
    {
        if ((flags & FLAG_NORMAL) != 0u)
        {
            float4 nm = sampleAtlas(normalTex, cellUv(f0, grid, i.uv), dx, dy);
            if (blend > 0.004)
                nm = lerp(nm, sampleAtlas(normalTex, cellUv(f1, grid, i.uv), dx, dy), blend);
            n.xy = nm.rg * 2.0 - 1.0;
            n.z = sqrt(saturate(1.0 - dot(n.xy, n.xy)));
            thickness = nm.b;
        }
        else
        {
            const float2 p = float2(i.uv.x * 2.0 - 1.0, 1.0 - i.uv.y * 2.0);
            n = normalize(float3(p * 0.8, sqrt(saturate(1.0 - dot(p, p))) + 0.3));
        }
    }
    const float3 N = normalize(i.axisX * n.x + i.axisY * n.y + i.back * n.z);
    const float3 L = normalize(pc.sunDir.xyz);
    const float wrapK = 0.15;
    const float wrap = saturate((dot(N, L) + wrapK) / (1.0 + wrapK));
    const float scatter = pow(saturate(dot(-i.back, L)), 6.0) * (1.0 - thickness) * asfloat(pc.ctl.w);
    const float selfShadow = lerp(1.0, wrap, 0.5 + 0.5 * thickness);
    const float3 light = pc.sunColour.rgb * pc.sunDir.w * (wrap * selfShadow * i.fog.y + scatter * lerp(0.45, 1.0, i.fog.y)) + pc.sky.rgb * float3(0.76, 0.86, 1.04) * pc.sunColour.w * (0.55 + 0.2 * N.y);
    const float3 albedo = (lit ? tex.rgb : float3(1, 1, 1)) * i.colour.rgb;

     
    float3 em = emission;
    const float peak = max(em.r, max(em.g, em.b));
    em = lerp(em, float3(peak, peak, peak), saturate((peak - 1.0) * 0.3));

     
    const float3 rgb = albedo * light * alpha + em;
    return float4(rgb, alpha);
}
