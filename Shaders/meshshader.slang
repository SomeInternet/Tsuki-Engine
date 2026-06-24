struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;

    float3 camPos;

    float ambient;
};

struct TsukiMaterialData {
    int colorTextureIndex;
    int metallicRoughnessTextureIndex;
    int emissiveTextureIndex;
    int normalTextureIndex;

    float4 colorFac;
    float4 metallicRoughnessFac;
    float4 emissionFac;

    uint8_t pass;
    uint8_t passPadding[3];
    float4 padding[11];
};

struct Vertex {
    float3 pos;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
};

struct PushConstants {
    float4x4 worldMatrix;
    float4x4 inverseTranspose;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 worldPos;
    float3 normal;
    float3 color;
    float2 uv;
    nointerpolation int materialIndex;
};

[vk::binding(0, 0)]
ConstantBuffer<SceneData> sceneData;

[vk::binding(0, 1)]
StructuredBuffer<TsukiMaterialData> materialDataArray;

[vk::binding(1, 1)]
Texture2D<float4> textures[4096];

[vk::binding(2, 1)]
SamplerState defaultSampler;

[vk::binding(0, 2)]
StructuredBuffer<Vertex> vertexBuffer;

[vk::binding(1, 2)]
StructuredBuffer<int> materialLookupBuffer;

[vk::push_constant]
PushConstants vsInput;

[shader("vertex")]
VSOutput vertMain(uint index : SV_VertexID) {
    Vertex v = vertexBuffer[index];

    float4 pos = float4(v.pos, 1);

    VSOutput vsOutput;
    vsOutput.pos = mul(sceneData.viewproj, mul(vsInput.worldMatrix, pos));
    vsOutput.worldPos = mul(vsInput.worldMatrix, pos).xyz;
    vsOutput.normal = mul(vsInput.inverseTranspose, float4(v.normal, 1)).xyz;

    uint materialIndex = materialLookupBuffer[index];
    TsukiMaterialData mat = materialDataArray[materialIndex];
    vsOutput.color = v.color.rgb;
    vsOutput.uv.x = v.uvX;
    vsOutput.uv.y = v.uvY;
    vsOutput.materialIndex = materialIndex;

    return vsOutput;
}

[shader("fragment")]
float4 fragMain(VSOutput fsInput) : SV_Target {
    float3 camPos = mul(sceneData.view, float4(0, 0, 0, 1)).xyz;

    float lambertian = max(dot(normalize(fsInput.normal), normalize(sceneData.camPos - fsInput.worldPos)), sceneData.ambient);

    if (fsInput.materialIndex == -1) {
        return float4(1, 0, 1, 1);
    }

    bool hasMat = false;
    TsukiMaterialData mat = materialDataArray[fsInput.materialIndex];

    float3 texColor = float3(1, 1, 1);
    float3 emissionColor = mat.emissionFac.rgb;
    if (mat.colorTextureIndex >= 0) {
        texColor = textures[NonUniformResourceIndex(mat.colorTextureIndex)].Sample(defaultSampler, fsInput.uv).rgb;
    }
    if (mat.emissiveTextureIndex >= 0) {
        emissionColor = textures[NonUniformResourceIndex(mat.emissiveTextureIndex)].Sample(defaultSampler, fsInput.uv).rgb;
    }
    float3 color = fsInput.color * mat.colorFac.rgb;

    return max(float4(lambertian * color, 1), float4(emissionColor, 1));
}
