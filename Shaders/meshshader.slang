struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;

    float3 camPos;

    float ambient;
};

struct GLTFMaterialData {
    float4 colorFac; //Like a BRDF
    float4 metallicRoughnessFac; //PBR parameters
};

struct Vertex {
    float3 pos;
    float uvX;
    float3 normal;
    float uvY;
    float4 color;
    float4 emission;
};

struct PushConstants {
    float4x4 worldMatrix;
    float4x4 inverseTranspose; //4x4 for memory alignment reasons--rows get 16 bytes each, whereas the CPU tightly packs the floats (I think)
    Vertex *vertexBuffer;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 worldPos;
    float3 normal;
    float3 color;
    float2 uv;
    float4 emission;
};

//Slang will attempt to pack loose global structures together
//vk::binding(binding, set)
[vk::binding(0, 0)]
ConstantBuffer<SceneData> sceneData;

[vk::binding(0, 1)]
ConstantBuffer<GLTFMaterialData> gltfMaterialData;

[vk::binding(1, 1)]
Sampler2D colorTexture;

[vk::binding(2, 1)]
Sampler2D metallicRoughnessTexture;

[vk::push_constant]
PushConstants vsInput;

[shader("vertex")]
VSOutput vertMain(uint index: SV_VertexID) {
    Vertex v = vsInput.vertexBuffer[index];

    float4 pos = float4(v.pos, 1);

    VSOutput vsOutput;
    vsOutput.pos = mul(sceneData.viewproj, mul(vsInput.worldMatrix, pos));
    vsOutput.worldPos = mul(vsInput.worldMatrix, pos).xyz;
    vsOutput.normal = mul(vsInput.inverseTranspose, float4(v.normal, 1)).xyz;
    vsOutput.color = gltfMaterialData.colorFac.rgb;
    vsOutput.uv.x = v.uvX;
    vsOutput.uv.y = v.uvY;
    vsOutput.emission = v.emission;

    return vsOutput;
}

[shader("fragment")]
float4 fragMain(VSOutput fsInput) : SV_Target {
    float3 camPos = mul(sceneData.view, float4(0, 0, 0, 1)).xyz;

    float lambertian = max(dot(normalize(fsInput.normal), normalize(sceneData.camPos - fsInput.worldPos)), sceneData.ambient);
    float3 color = fsInput.color * colorTexture.Sample(fsInput.uv).rgb;

    //return float4(fsInput.normal, 1);
    //return float4(fsInput.color, 1);
    return float4(lambertian * color, 1);
}