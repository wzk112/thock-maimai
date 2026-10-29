#include <metal_stdlib>
using namespace metal;

struct MaimaiQuad {
    float4 position [[position]];
    float2 uv;
};

vertex MaimaiQuad maimaiHDRVertex(uint id [[vertex_id]]) {
    float2 p = float2((id << 1) & 2, id & 2);
    return {float4(p * 2.0 - 1.0, 0.0, 1.0), float2(p.x, 1.0 - p.y)};
}

fragment float4 maimaiHDRFragment(MaimaiQuad in [[stage_in]],
                                 texture2d<float> source [[texture(0)]],
                                 constant float &headroom [[buffer(0)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    float4 c = source.sample(s, in.uv);
    // SpriteKit's existing SDR pass is premultiplied sRGB. Linearize the
    // unassociated color, then premultiply again for the desktop compositor.
    if (c.a <= 0.00001) return float4(0.0);
    float3 rgb = clamp(c.rgb / c.a, 0.0, 1.0);
    float3 linear = select(pow((rgb + 0.055) / 1.055, float3(2.4)),
                           rgb / 12.92, rgb <= 0.04045);
    float peak = max(linear.r, max(linear.g, linear.b));
    float range = max(headroom, 1.0);
    // Bright cores reach the display's available peak without bleaching their
    // hue. Keep SDR identical when HDR is off or no extra headroom is available.
    float highlight = range > 1.001 ? smoothstep(0.55, 0.9, peak) : 0.0;
    float gain = mix(1.0, range / max(peak, 0.00001), highlight);
    return float4(linear * gain * c.a, c.a);
}
