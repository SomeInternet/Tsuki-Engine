struct Vertex {
    float3 pos;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    nointerpolation uint materialIndex : TEXCOORD0;
};

struct PushConstants {
    float4x4 renderMatrix;
};

struct GLTFMaterialData {
    int colorTextureIndex;
    int metallicRoughnessTextureIndex;
    int emissiveTextureIndex;
    int normalTextureIndex;

    float4 colorFac;
    float4 metallicRoughnessFac;
    float4 emissionFac;

    uint pass;
    uint passPadding[3];
    float4 padding[11];
};

struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;

    float3 camPos;

    float ambient;
};

[vk::push_constant]
PushConstants vsInput;

[vk::binding(0, 0)]
ConstantBuffer<SceneData> sceneData;

[vk::binding(0, 1)]
StructuredBuffer<GLTFMaterialData> materialDataArray;

[vk::binding(1, 1)]
Texture2D<float4> textures[];

[vk::binding(2, 1)]
SamplerState defaultSampler;

[vk::binding(0, 2)]
StructuredBuffer<Vertex> vertexBuffer;

[vk::binding(1, 2)]
StructuredBuffer<uint> materialLookupBuffer;

[shader("vertex")]
VSOutput vertMain(uint index : SV_VertexID) {
    Vertex v = vertexBuffer[index];

    VSOutput vsOutput;
    vsOutput.pos = mul(vsInput.renderMatrix, float4(v.pos, 1));
    vsOutput.uv = float2(v.uvX, v.uvY);
    vsOutput.color = v.color.rgb;

    uint materialIndex = materialLookupBuffer[index];
    vsOutput.materialIndex = materialIndex;

    return vsOutput;
}

[shader("fragment")]
float4 fragMain(VSOutput fsInput) : SV_Target {
    return float4(fsInput.color, 1);
}

[shader("fragment")]
float4 fragTextureMain(VSOutput fsInput) : SV_Target {
    GLTFMaterialData mat = materialDataArray[fsInput.materialIndex];
    float3 texColor = float3(1, 1, 1);
    if (mat.colorTextureIndex >= 0) {
        texColor = textures[NonUniformResourceIndex(mat.colorTextureIndex)].Sample(defaultSampler, fsInput.uv).rgb;
    }
    return float4(texColor, 1);
}
