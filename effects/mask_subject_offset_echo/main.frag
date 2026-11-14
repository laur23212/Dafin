#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 offset;
uniform float echoOpacity;
uniform float edgeSoftness;
uniform vec3 echoColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));vec4 s=texture(u_currentTexture,uv);
 if(u_hasClipMask<.5){fragColor=s;return;}
 vec2 q=uv-offset;
 float behind=driftMask(clamp(q,vec2(0.0),vec2(1.0)))*(1.0-driftMask(uv));
 float strength=clamp(behind*echoOpacity,0.0,1.0);
 vec3 rgb=mix(s.rgb,echoColor,strength*(1.0-edgeSoftness*.5));
 fragColor=vec4(rgb,s.a);
}