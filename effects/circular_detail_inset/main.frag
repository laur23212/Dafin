#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 center;
uniform float radius;
uniform float magnification;
uniform float ringWidth;
uniform vec3 ringColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 vec4 a=texture(u_currentTexture,uv);
 vec2 rel=uv-center;rel.x*=1.0;
 float d=length(rel);
 vec2 q=clamp(center+rel/max(magnification,1.0),vec2(0.0),vec2(1.0));
 vec3 closeUp=texture(u_currentTexture,q).rgb;
 float fill=1.0-smoothstep(radius-.005,radius+.002,d);
 float border=smoothstep(radius-ringWidth-.004,radius-ringWidth,d)*(1.0-smoothstep(radius,radius+.004,d));
 vec3 rgb=mix(a.rgb,closeUp,fill);
 rgb=mix(rgb,ringColor,border);
 fragColor=vec4(rgb,a.a);
}