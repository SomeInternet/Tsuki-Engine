enum ToneMapMode {
    TONE_REINHARDT, TONE_ACES, TONE_AGX
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

float3 rrtAndOdtFit(float3 v) {
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 acesFitted(float3 color) {
    return clamp(mul(ACESOutputMat, rrtAndOdtFit(mul(ACESInputMat, color))), float3(0), float3(1));
}

//AgX approximation
static const float3x3 agXInsetMatrix = {
    0.8566271394, 0.0951212407, 0.0482516199,
    0.1373189745, 0.7607997357, 0.1018812898,
    0.1118982129, 0.0767994183, 0.8113023688
};

static const float3x3 agXOutsetMatrix = {
     1.1968798066, -0.0980208811, -0.0988591255,
    -0.0528968517,  1.1519031299, -0.0990062782,
    -0.0577730035, -0.0987018299,  1.1564748292
};

float3 applyAgXContrast(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  15.5    * x4 * x2
         - 40.14    * x4 * x
         + 31.96    * x4
         -  6.868   * x2 * x
         +  0.4298  * x2
         +  0.1191  * x
         -  0.00232;
}

float3 agXApprox(float3 color) {
    const float minEv = -12.47393f;
    const float maxEv =   4.026069f;

    float3 val = mul(agXInsetMatrix, color);

    val = max(val, 1e-10f);
    val = clamp(log2(val), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);

    val = applyAgXContrast(val);

    float3 contrastingColor = smoothstep(0.0, 1.0, val);
    val = lerp(val, contrastingColor, 0.25);

    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    val = luma + 1.15 * (val - luma);

    return mul(agXOutsetMatrix, clamp(val, 0.0, 1.0));
}

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint3 globalId: SV_DispatchThreadID, uint3 localId: SV_GroupThreadID) { //SV means system value. We specified the dimensions of the workgroups, and in the dispatch command we specified the number of workgroups

    uint2 size;
    image.GetDimensions(size.x, size.y); //Store image dimensions in x, y

    if (globalId.x < size.x && globalId.y < size.y) {
        float3 color;
        ToneMapMode mode = toneMapMode;
        switch (mode) {
        case ToneMapMode::TONE_REINHARDT:
            color = reinhardt(image[globalId.xy].rgb);
            break;

        case ToneMapMode::TONE_ACES:
            color = acesFitted(image[globalId.xy].rgb);
            break;

        case ToneMapMode::TONE_AGX:
            color = agXApprox(image[globalId.xy].rgb);
            break;
        }

        if (mode != ToneMapMode::TONE_AGX) {
            color = gammaCorrect(color);
        }
        
        image[globalId.xy] = float4(color, 1.f);
    }
}