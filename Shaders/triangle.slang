static float2 positions[3] = {
    float2(0, -.5),
    float2(.5, .5),
    float2(-.5, .5)
};

static float3 colors[3] = {
    float3(1, 0, 0),
    float3(0, 1, 0),
    float3(0, 0, 1)
};

struct VSOutput {
    float4 svPosition : SV_Position;
    float3 color;
};

[shader("vertex")]
VSOutput vertMain(uint vertIndex : SV_VertexID) { //SV_VertexID denotes the index of the invocation of the vertex shader
    VSOutput output;
    output.svPosition = float4(positions[vertIndex], 0, 1);
    output.color = colors[vertIndex];

    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target { //SV_Target tells to output to render target 0 (?)
    float3 color = input.color;
    return float4(color, 1);
}