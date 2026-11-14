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
uniform float amount;
uniform float pivot;
uniform vec3 shadowTint;
uniform vec3 highlightTint;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);float Y=lum(s.rgb);
 float sh=1.-smoothstep(pivot-.27,pivot+.04,Y);
 float hi=smoothstep(pivot-.02,pivot+.31,Y);
 vec3 t=mix(s.rgb,s.rgb*.68+shadowTint*.32,amount*sh);
 t=mix(t,t*.69+highlightTint*.31,amount*hi);
 fragColor=vec4(clamp(t,0.,1.),s.a);}
