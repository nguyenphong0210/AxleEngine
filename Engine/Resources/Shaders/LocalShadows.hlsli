cbuffer LocalShadowReceiver : register(b5,space0) {
    float4x4 u_LocalShadowMatrices[16];
    float4 u_LocalShadowTiles[16];
    float4 u_LocalShadowBias[16];
    int4 u_LocalShadowViews[8];
    float4 u_LocalShadowAtlas;
};
Texture2D<float> u_LocalShadowDepth : register(t16,space1);
float3 LocalReceiverPosition(uint view,float3 world,float3 normal,float3 light) {
    float grazing=1-saturate(dot(normal,light));
    float4 bias=u_LocalShadowBias[view];
    return world+normal*bias.z*grazing+light*(bias.x+bias.y*grazing);
}
float LocalShadowVisibility(int view,float3 world,float3 normal,float3 light) {
    if(view<0||view>=(int)u_LocalShadowAtlas.y)return 1;
    float4 clip=mul(u_LocalShadowMatrices[view],float4(LocalReceiverPosition(view,world,normal,light),1));
    if(clip.w<=0)return 1;
    float3 projected=clip.xyz/clip.w;
    if(any(abs(projected.xy)>1)||projected.z<0||projected.z>1)return 1;
    float4 tile=u_LocalShadowTiles[view];
    float2 uv=tile.xy+(projected.xy*float2(.5,-.5)+.5)*tile.z;
    float texel=u_LocalShadowAtlas.x,halfTexel=texel*.5,visibility=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x) {
        float2 tap=clamp(uv+float2(x,y)*texel*u_LocalShadowBias[view].w,tile.xy+halfTexel,tile.xy+tile.z-halfTexel);
        visibility+=u_LocalShadowDepth.SampleCmpLevelZero(u_ShadowSampler,tap,projected.z);
    }
    return visibility/9;
}
float PointShadowVisibility(uint index,float3 position,float3 world,float3 normal,float3 light) {
    int first=u_LocalShadowViews[index/4][index%4];
    if(first<0)return 1;
    float3 direction=LocalReceiverPosition(first,world,normal,light)-position;
    float3 axis=abs(direction);int face;
    if(axis.x>=axis.y&&axis.x>=axis.z)face=direction.x>=0?0:1;
    else if(axis.y>=axis.z)face=direction.y>=0?2:3;
    else face=direction.z>=0?4:5;
    return LocalShadowVisibility(first+face,world,normal,light);
}
