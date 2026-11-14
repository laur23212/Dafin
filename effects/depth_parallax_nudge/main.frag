#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float shift;
uniform float nearBias;
uniform float sway;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mirrored(vec2 q){return 1.0-abs(mod(q,2.0)-1.0);}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));vec4 src=texture(u_currentTexture,uv);
 if(u_hasDepth<.5){fragColor=src;return;}
 float depth=clamp(driftDepthGuided(uv,u_currentTexture),0.0,1.0);
 float t=float(u_frameIndex)/max(u_frameRate,1.0);
 float amount=shift+sin(t*1.4)*sway;
 vec2 delta=vec2(amount,-amount*.30)*mix(.25,1.0,smoothstep(0.05,1.0,depth))*nearBias;
 vec2 q=mirrored(uv+delta);
 vec4 moved=texture(u_currentTexture,clamp(q,vec2(0.0),vec2(1.0)));
 fragColor=vec4(moved.rgb,src.a);
}