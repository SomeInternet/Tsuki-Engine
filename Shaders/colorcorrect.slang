enum ToneMapMode {
    TONE_REINHARDT, TONE_ACES
};

[[vk::binding(0, 0)]] //Binding 0, descriptor set 0
RWTexture2D<float4> image;

[vk::push_constant]
ToneMapMode toneMapMode;

float3 reinhardt(float3 color) {
    return color / (color + float3(1));
}

float3 gammaCorrect(float3 color) {
    return pow(color, 1.f / 2.2f);
}

//ACES Approximation (https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl)
static const float3x3 ACESInputMat =
{
    { .59719f, .35458f, .04823f },
    { .07600f, .90834f, .01566f },
    { .02840f, .13383f, .83777f }
};

static const float3x3 ACESOutputMat =
{
    { 1.60475f, -0.53108f, -0.07367f },
    { -0.10208f, 1.10813f, -0.00605f },
    { -0.00327f, -0.07276f, 1.07602f }
};

float3 rrtAndOdtFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 acesFitted(float3 color) {
    return clamp(mul(ACESOutputMat, rrtAndOdtFit(mul(ACESInputMat, color))), float3(0), float3(1));
}

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 globalId: SV_DispatchThreadID, uint3 localId: SV_GroupThreadID) { //SV means system value. We specified the dimensions of the workgroups, and in the dispatch command we specified the number of workgroups

    uint2 size;
    image.GetDimensions(size.x, size.y); //Store image dimensions in x, y

    if (globalId.x < size.x && globalId.y < size.y) {
        float3 color = gammaCorrect(select(toneMapMode == ToneMapMode::TONE_REINHARDT, reinhardt(image[globalId.xy].rgb), acesFitted(image[globalId.xy].rgb)));
        
        image[globalId.xy] = float4(color, 1.f);
    }
}