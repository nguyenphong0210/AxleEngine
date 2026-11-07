#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec4 border;
layout(location=4) in vec2 size;
layout(location=5) in float radius;
layout(location=6) in float borderWidth;
layout(location=7) in vec2 angleRange;
layout(location=8) in uint textureIndex;
layout(location=9) in uint primitive;
uniform vec2 viewport;
out vec2 vUV; out vec4 vColor; out vec4 vBorder; out vec2 vSize;
out float vRadius; out float vBorderWidth; out vec2 vAngle; flat out uint vPrimitive;
void main(){ gl_Position=vec4(position/viewport*vec2(2,-2)+vec2(-1,1),0,1);
vUV=uv; vColor=color; vBorder=border; vSize=size; vRadius=radius; vBorderWidth=borderWidth; vAngle=angleRange; vPrimitive=primitive; }
