struct FullscreenVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[shader("vertex")]]
FullscreenVertex main(uint vertex : SV_VertexID)
{
    const float2 uv = float2((vertex << 1) & 2, vertex & 2);
    FullscreenVertex result;
    result.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    result.uv = float2(uv.x, 1.0 - uv.y);
    return result;
}
