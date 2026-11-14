#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mirrorUV(vec2 v){vec2 t=mod(v,2.0);return 1.0-abs(t-1.0);}
float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
float rand2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float tm(){return float(u_frameIndex)/max(1.0,u_frameRate);}
uniform float reach;
uniform float threshold;
uniform float mixAmount;
uniform vec3 tint;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);
 vec3 sum=vec3(0.);float weight=0.;
 for(int i=-10;i<=10;i++){float d=float(i)/10.0;vec2 off=vec2(d*mix(.015,.30,reach),0.);
 vec3 src=texture(u_currentTexture,clamp(uv+off,vec2(0.),vec2(1.))).rgb;
 float hot=smoothstep(threshold,min(.999,threshold+.24),luma(src));
 float w=exp(-abs(d)*3.3)*hot;sum+=src*w;weight+=w;}
 vec3 light=(sum/max(weight,.00001))*min(1.,weight*.48);
 vec3 result=clamp(s.rgb+light*tint*mixAmount*1.35,0.,1.);
 fragColor=vec4(result,s.a);}
