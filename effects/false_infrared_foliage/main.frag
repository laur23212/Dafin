#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mir(vec2 a){vec2 t=mod(a,2.0);return 1.0-abs(t-1.0);}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float rnd(vec2 x){return fract(sin(dot(x,vec2(127.1,311.7)))*43758.5453123);}
uniform float intensity;
uniform float leafSelect;
uniform float skyCool;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);vec3 c=s.rgb;
 float green=clamp((c.g-max(c.r,c.b)*.7)*2.2+leafSelect*.18,0.,1.);
 float mask=smoothstep(.09,.54,green);
 vec3 pink=vec3(.92,.24,.55)*(.48+lum(c)*.78);
 vec3 cold=vec3(c.b*.45+c.r*.1, c.b*.7+c.g*.2,c.g*.68+c.b*.35);
 vec3 falsec=mix(mix(c,cold,skyCool),pink,mask);
 fragColor=vec4(mix(c,clamp(falsec,0.,1.),intensity),s.a);}
