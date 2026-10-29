// Copyright (c) 2026 Firefly112. MIT. Generic renderer, no game shader included.
cbuffer Parameters : register(b0) {
    float4 geometry; // center x/y, diameter, rotation (pixels/radians)
    float4 tint;
    float4 frame; // viewport width/height, time, exposure
    float4 options; // material, HDR range, SDR white / 80 nits, unused
};
Texture2D firstTexture : register(t0);
Texture2D secondTexture : register(t1);
SamplerState textureSampler : register(s0);
struct Vertex { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
Vertex SpriteVertex(uint id:SV_VertexID) {
    const float2 coords[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    float2 uv=coords[id], p=(uv-.5)*geometry.z;
    float c=cos(geometry.w),s=sin(geometry.w);
    p=float2(c*p.x-s*p.y,s*p.x+c*p.y)+geometry.xy;
    Vertex o; o.position=float4(p.x/frame.x*2-1,1-p.y/frame.y*2,0,1);o.uv=uv;return o;
}
float4 SpritePixel(Vertex i):SV_TARGET {
    float4 tex=firstTexture.Sample(textureSampler,i.uv);
    float3 rgb=tex.rgb;
    if(options.x==50)rgb*=secondTexture.Sample(textureSampler,i.uv).rgb;
    rgb*=exp2(frame.w)*tint.rgb;
    float a=tex.a*tint.a;
    return float4(rgb*a,saturate(max(rgb.r,max(rgb.g,rgb.b))*a));
}
Vertex FullVertex(uint id:SV_VertexID) {
    Vertex o; o.uv=float2((id<<1)&2,id&2); o.position=float4(o.uv.x*2-1,1-o.uv.y*2,0,1);return o;
}
float3 toLinear(float3 v) {
    return float3(v.r<=.04045?v.r/12.92:pow((v.r+.055)/1.055,2.4),
                  v.g<=.04045?v.g/12.92:pow((v.g+.055)/1.055,2.4),
                  v.b<=.04045?v.b/12.92:pow((v.b+.055)/1.055,2.4));
}
float4 FinishPixel(Vertex i):SV_TARGET {
    float4 color=firstTexture.Sample(textureSampler,i.uv);
    if(color.a<=.00001)return 0;
    float3 linear=toLinear(saturate(color.rgb/color.a));
    float peak=max(linear.r,max(linear.g,linear.b));
    float high=options.y>1.001?smoothstep(.55,.9,peak):0;
    float gain=lerp(1,options.y/max(peak,.00001),high);
    return float4(linear*gain*options.z*color.a,color.a);
}
