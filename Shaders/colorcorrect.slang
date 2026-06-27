[[vk::binding(0, 0)]] //Binding 0, descriptor set 0
RWTexture2D<float4> image;

float3 reinhardt(float3 color) {
    return color / (color + float3(1));
}

float3 gammaCorrect(float3 color) {
    return pow(color, 1.f / 2.2f);
}

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 globalId: SV_DispatchThreadID, uint3 localId: SV_GroupThreadID) { //SV means system value. We specified the dimensions of the workgroups, and in the dispatch command we specified the number of workgroups

    uint2 size;
    image.GetDimensions(size.x, size.y); //Store image dimensions in x, y

    if (globalId.x < size.x && globalId.y < size.y) {
        float3 color = gammaCorrect(reinhardt(image[globalId.xy].rgb));
        image[globalId.xy] = float4(color, 1.f);
    }
}