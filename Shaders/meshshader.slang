struct SceneData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;

    float4 ambient;
    float4 lightDir;
    float4 lightColor;
};

struct GLTFMaterialData {
    float4 colorFac;
    float4 metallicRoughnessFac;
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
    Vertex *vertexBuffer;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 normal;
    float3 color;
    float2 uv;
};

//Slang will attempt to pack loose global structures together
[vk_binding(0, 0)]
ConstantBuffer<SceneData> sceneData;

[vk_binding(1, 0)]
ConstantBuffer<GLTFMaterialData> gltfMaterialData;

[vk_binding(1, 1)]
Sampler2D colorTexture;

[vk_binding(1, 2)]
Sampler2D metallicRoughnessTexture;

[vk::push_constant]
PushConstants vsInput;

[shader("vertex")]
VSOutput vertMain(uint index: SV_VertexID) {
    Vertex v = vsInput.vertexBuffer[index];

    float4 pos = float4(v.pos, 1);

    VSOutput vsOutput;
    vsOutput.pos = mul(sceneData.viewproj, mul(vsInput.worldMatrix, pos));
    vsOutput.normal = mul(vsInput.worldMatrix, float4(v.normal, 0)).xyz;
    vsOutput.color = mul(v.color.rgb, gltfMaterialData.colorFac.rgb);
    vsOutput.uv.x = v.uvX;
    vsOutput.uv.y = v.uvY;

    return vsOutput;
}

[shader("fragment")]
float4 fragMain(VSOutput fsInput) : SV_Target {
    float lambertian = max(dot(fsInput.normal, sceneData.lightDir.xyz), .1f);
    float3 color = mul(fsInput.color, colorTexture.Sample(fsInput.uv).rgb);
    float3 ambient = mul(color, sceneData.ambient.rgb);

    return float4(mul(lambertian, mul(sceneData.lightColor.a, color)) + ambient, 1);
}