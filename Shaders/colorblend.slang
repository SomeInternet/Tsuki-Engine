struct PushConstants {
    float4 data1;
    float4 data2;
    float4 data3;
    float4 data4;
};

[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

[vk::push_constant]
PushConstants pushConstants;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 globalId: SV_DispatchThreadID, uint3 localId: SV_GroupThreadID) {
    float4 topColor = pushConstants.data1;
    float4 botColor = pushConstants.data2;

    uint2 size;
    image.GetDimensions(size.x, size.y); // Store image dimensions in x, y

    if (globalId.x < size.x && globalId.y < size.y) {
        float blend = float(globalId.y) / size.y;

        image[globalId.xy] = lerp(topColor, botColor, blend);
    }
}