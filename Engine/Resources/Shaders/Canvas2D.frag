#version 450 core
in vec2 vUV; in vec4 vColor; in vec4 vBorder; in vec2 vSize;
in float vRadius; in float vBorderWidth; in vec2 vAngle; flat in uint vPrimitive;
uniform sampler2D image;
layout(location=0) out vec4 outputColor;
void main(){
 if(vPrimitive==0u){vec2 p=(vUV-.5)*vSize;vec2 b=.5*vSize;float r=min(vRadius,min(b.x,b.y));vec2 q=abs(p)-b+r;
 float d=length(max(q,0))+min(max(q.x,q.y),0)-r;float a=clamp(.5-d,0,1);vec4 c=vColor;
 if(vBorderWidth>0)c=mix(vBorder,vColor,clamp(.5-d-vBorderWidth,0,1));outputColor=c*vec4(1,1,1,a);}
 else if(vPrimitive==1u)outputColor=texture(image,vUV)*vColor;
 else if(vPrimitive==4u)outputColor=vColor*vec4(1,1,1,texture(image,vUV).a);
 else if(vPrimitive==2u){vec3 m=texture(image,vUV).rgb;float sd=max(min(m.r,m.g),min(max(m.r,m.g),m.b))-.5;
 float range=max(.5*dot(vec2(4)/max(vSize,vec2(1)),vec2(1)/max(fwidth(vUV),vec2(.0001))),1);outputColor=vColor*vec4(1,1,1,clamp(sd*range+.5,0,1));}
 else {vec2 p=(vUV-.5)*vSize;float r=min(vSize.x,vSize.y)*.5;float thickness=max(vRadius,1);
 float alpha=clamp(.5-(abs(length(p)-(r-.5*thickness))-.5*thickness),0,1);float angle=atan(p.y,p.x);
 const float twoPi=6.28318530718;
 float span=mod(mod(vAngle.y-vAngle.x,twoPi)+twoPi,twoPi);if(span<=.001)span=twoPi;
 float a=mod(mod(angle-vAngle.x,twoPi)+twoPi,twoPi);
 float activeSpan=clamp(vBorderWidth-vAngle.x,0,span);
 outputColor=(activeSpan>0&&a<=activeSpan?vColor:vBorder)*vec4(1,1,1,a<=span?alpha:0);}
}
