// C13-01 depth-only caster pass. Object layout matches RHIObjectPushConstants.
struct ObjectData {
    float4x4 Model;
    float4 Primary;
    float4 Secondary;
    float4 Params;
    float4 Extra;
};
#if defined(__spirv__)
[[vk::push_constant]] ObjectData Object;
#else
cbuffer ObjectConstants : register(b0,space0) { ObjectData Object; };
#endif
cbuffer Camera : register(b1,space0) { float4x4 LightViewProjection; };
cbuffer Material : register(b3,space0) {
    float4 UVTransform;
    float4 Surface;
    float4 Coat;
    uint4 Flags;
};
Texture2D Albedo : register(t0,space1);
SamplerState MaterialSampler : register(s0,space0);
struct Input {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};
struct Pixel {
    float4 Position : SV_Position;
    float3 World : TEXCOORD0;
    float3 Local : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float4 Color : COLOR0;
};
Pixel VSMain(Input input) {
    Pixel p;
    float4 world=mul(Object.Model,float4(input.Position,1));
    p.Position=mul(LightViewProjection,world);
    p.World=world.xyz; p.Local=input.Position; p.UV=input.UV; p.Color=input.Color;
    return p;
}
void PSMain(Pixel p) {
    if ((int)Object.Extra.w!=1) return; // Opaque ignores texture alpha.
    int mode=(int)Object.Params.y;
    float scale=max(.01,Object.Params.x);
    float alpha=Object.Primary.a;
    float pattern=0;
    if (mode==0) {
        float2 cell=floor(p.World.xz*scale); pattern=cell.x+cell.y;
    } else if (mode==1) {
        float3 cell=floor((p.Local+.5)*scale+.0001); pattern=cell.x+cell.y+cell.z;
    } else if (mode==2) {
        float3 n=normalize(p.Local);
        float2 uv=float2(atan2(n.z,n.x)/(2*3.14159265359)+.5,asin(clamp(n.y,-1,1))/3.14159265359+.5);
        float2 cell=floor(uv*float2(scale,scale*.5)); pattern=cell.x+cell.y;
    }
    if (mode<=2) alpha=lerp(Object.Primary.a,Object.Secondary.a,pattern-2*floor(pattern/2));
    if (mode==4) alpha*=p.Color.a;
    if ((mode==5 || mode==6) && (Flags.x&1u)!=0) alpha*=Albedo.Sample(MaterialSampler,p.UV*UVTransform.xy+UVTransform.zw).a;
    clip(alpha-saturate(Object.Extra.z));
}
