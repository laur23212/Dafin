#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform int u_frameIndex;
uniform float u_frameRate;
uniform float amount;
uniform float rippleRate;
uniform float farLimit;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));vec4 src=texture(u_currentTexture,uv);
 if(u_hasDepth<.5){fragColor=src;return;}
 float depth=clamp(driftDepthGuided(uv,u_currentTexture),0.0,1.0);
 float distant=1.0-smoothstep(farLimit,farLimit+.25,depth);
 float t=float(u_frameIndex)/max(u_frameRate,1.0);
 float wiggle=sin(uv.y*47.0+rippleRate*t+sin(uv.x*16.0)*2.1)*.65+sin(uv.y*86.0-rippleRate*t*.77)*.35;
 vec2 q=clamp(uv+vec2(amount*wiggle*distant,0.0),vec2(0.0),vec2(1.0));
 vec4 warp=texture(u_currentTexture,q);
 fragColor=vec4(warp.rgb,src.a);
}