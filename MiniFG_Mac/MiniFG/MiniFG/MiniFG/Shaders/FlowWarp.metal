#include <metal_stdlib>
using namespace metal;

struct FlowConfidenceParams {
    uint  hasReverseFlow;
    float consistencyBias;
    float consistencyScale;
    float fallbackConfidence;
};

struct FlowSynthesisParams {
    float blendFactor;
    uint  hasReverseFlow;
    uint  splitScreen;
    uint  reserved;
};

constexpr sampler kLinearClampSampler(filter::linear, address::clamp_to_edge);

static inline bool inBounds(float2 pixel, float2 size)
{
    return all(pixel >= 0.0f) && pixel.x <= (size.x - 1.0f) && pixel.y <= (size.y - 1.0f);
}

static inline float2 pixelToUV(float2 pixel, float2 size)
{
    return (pixel + 0.5f) / size;
}

static inline float3 hsvToRgb(float3 hsv)
{
    float4 k = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(fract(hsv.xxx + k.xyz) * 6.0f - k.www);
    return hsv.z * mix(k.xxx, clamp(p - k.xxx, 0.0f, 1.0f), hsv.y);
}

kernel void estimateConfidence(
    texture2d<float, access::read> forwardFlow     [[texture(0)]],
    texture2d<float, access::read> reverseFlow     [[texture(1)]],
    texture2d<half,  access::write> confidenceMask [[texture(2)]],
    constant FlowConfidenceParams& params          [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= confidenceMask.get_width() || gid.y >= confidenceMask.get_height()) return;

    float2 size = float2(confidenceMask.get_width(), confidenceMask.get_height());
    float2 pixel = float2(gid);
    float2 forward = forwardFlow.read(gid).xy;
    float2 destination = pixel + forward;

    float confidence = 0.0f;
    if (inBounds(destination, size)) {
        if (params.hasReverseFlow != 0) {
            uint2 reverseSample = uint2(clamp(destination, float2(0.0f), size - 1.0f));
            float2 reverse = reverseFlow.read(reverseSample).xy;
            float error = length(forward + reverse);
            float threshold = params.consistencyBias +
                              params.consistencyScale * max(length(forward), length(reverse));
            confidence = 1.0f - smoothstep(0.0f, threshold, error);
        } else {
            float magnitude = length(forward);
            float attenuation = smoothstep(2.0f, 24.0f, magnitude);
            confidence = mix(params.fallbackConfidence, 0.25f, attenuation);
        }
    }

    confidenceMask.write(half4(half(clamp(confidence, 0.0f, 1.0f))), gid);
}

kernel void synthesizeFlow(
    texture2d<float, access::sample> prevColor       [[texture(0)]],
    texture2d<float, access::sample> currColor       [[texture(1)]],
    texture2d<float, access::read>   forwardFlow     [[texture(2)]],
    texture2d<float, access::read>   reverseFlow     [[texture(3)]],
    texture2d<half,  access::read>   confidenceMask  [[texture(4)]],
    texture2d<float, access::write>  outputTexture   [[texture(5)]],
    constant FlowSynthesisParams& params             [[buffer(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) return;

    float2 size = float2(outputTexture.get_width(), outputTexture.get_height());
    float2 pixel = float2(gid);
    float2 uv = pixelToUV(pixel, size);
    float t = clamp(params.blendFactor, 0.0f, 1.0f);

    float4 simpleBlend = mix(prevColor.sample(kLinearClampSampler, uv),
                             currColor.sample(kLinearClampSampler, uv),
                             t);

    float2 forward = forwardFlow.read(gid).xy;
    float2 prevSourcePixel = pixel - forward * t;
    float2 currSourcePixel = pixel + forward * (1.0f - t);
    if (params.hasReverseFlow != 0) {
        float2 reverse = reverseFlow.read(gid).xy;
        currSourcePixel = pixel - reverse * (1.0f - t);
    }

    bool prevValid = inBounds(prevSourcePixel, size);
    bool currValid = inBounds(currSourcePixel, size);
    float confidence = confidenceMask.read(gid).x;
    if (!prevValid || !currValid) confidence = 0.0f;

    float4 warpedPrev = prevColor.sample(kLinearClampSampler, pixelToUV(prevSourcePixel, size));
    float4 warpedCurr = currColor.sample(kLinearClampSampler, pixelToUV(currSourcePixel, size));
    float4 motionBlend = mix(warpedPrev, warpedCurr, t);

    float4 result = mix(simpleBlend, motionBlend, clamp(confidence, 0.0f, 1.0f));
    if (params.splitScreen != 0 && gid.x < (outputTexture.get_width() / 2)) {
        result = currColor.sample(kLinearClampSampler, uv);
    }

    outputTexture.write(result, gid);
}

kernel void visualizeFlow(
    texture2d<float, access::read> forwardFlow      [[texture(0)]],
    texture2d<float, access::write> outputTexture   [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) return;

    float2 flow = forwardFlow.read(gid).xy;
    float magnitude = clamp(length(flow) / 24.0f, 0.0f, 1.0f);
    float angle = atan2(flow.y, flow.x);
    float hue = fract((angle / (2.0f * M_PI_F)) + 1.0f);
    float3 rgb = hsvToRgb(float3(hue, 1.0f, max(magnitude, 0.15f)));
    outputTexture.write(float4(rgb, 1.0f), gid);
}

kernel void visualizeConfidence(
    texture2d<half, access::read> confidenceMask    [[texture(0)]],
    texture2d<float, access::write> outputTexture   [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= outputTexture.get_width() || gid.y >= outputTexture.get_height()) return;

    float confidence = clamp(float(confidenceMask.read(gid).x), 0.0f, 1.0f);
    float3 low = float3(0.85f, 0.12f, 0.12f);
    float3 high = float3(0.15f, 0.85f, 0.25f);
    float3 rgb = mix(low, high, confidence);
    outputTexture.write(float4(rgb, 1.0f), gid);
}
