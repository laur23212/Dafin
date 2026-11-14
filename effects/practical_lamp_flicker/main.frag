#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float sat(float x){return clamp(x,0.0,1.0);}
vec3 sat3(vec3 x){return clamp(x,vec3(0.0),vec3(1.0));}
float lum(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 px(){return 1.0/max(u_resolution,vec2(1.0));}
vec4 src(vec2 p){return texture(u_currentTexture,clamp(p,vec2(0.0001),vec2(0.9999)));}
float H(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}


uniform float warmth,strength,tempo,ambient;
void main(){vec4 c=src(v_texCoord);float t=float(u_frameIndex)/max(u_frameRate,1.);float w=mix(1.2,5.0,tempo);float a=.5+.5*sin(t*w*3.17+.2*sin(t*w*.79));float b=.5+.5*sin(t*w*7.71+1.3);float f=mix(a,b,.35);float gain=1.-strength*(1.-f)*.25;vec3 tint=vec3(1.,1.-warmth*.13,1.-warmth*.27);vec3 color=c.rgb*gain*tint;fragColor=vec4(sat3(mix(color,c.rgb,ambient*.22)),c.a);}

