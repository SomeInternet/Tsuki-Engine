struct Vertex {
    float3 pos;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

struct VSOutput {
    float4 pos : SV_Position; //SV_Position says that this is the clip-space position
    float3 color;
    float2 uv;
};

struct PushConstants {
    float4x4 renderMatrix;
    Vertex *vertices;
};

[vk::push_constant]
PushConstants vsInput;

[shader("vertex")]
VSOutput vertMain(uint index: SV_VertexID) { // SV_VertexID denotes the index of the invocation of the vertex shader
    Vertex v = vsInput.vertices[index];

    VSOutput vsOutput;
    vsOutput.pos = mul(vsInput.renderMatrix, float4(v.pos, 1));
    vsOutput.uv = float2(v.uvX, v.uvY);
    vsOutput.color = v.color.rgb;

    return vsOutput;
}

[shader("fragment")]
float4 fragMain(VSOutput fsInput) : SV_Target {
    return float4(fsInput.color, 1);
}