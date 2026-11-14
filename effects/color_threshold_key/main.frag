#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float threshold;
uniform float softness;
uniform float invert;
uniform float spillDarkness;
void main(){
 vec4 s=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 float lum=dot(s.rgb,vec3(.2126,.7152,.0722));
 float m=smoothstep(threshold-softness*.5,threshold+softness*.5+.0001,lum);
 m=mix(m,1.0-m,step(.5,invert));
 vec3 rgb=mix(s.rgb,s.rgb*max(m,.06),spillDarkness);
 fragColor=vec4(rgb,s.a*m);
}