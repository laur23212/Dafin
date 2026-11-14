#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float hashv(vec2 v){return fract(sin(dot(v,vec2(127.1,311.7)))*43758.5453);}
vec2 reflectUV(vec2 uv){return 1.0-abs(mod(uv,2.0)-1.0);}
vec4 inputAt(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.001),vec2(.999)));}
float safeTime(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float t=safeTime();float fold=sin(uv.x*24.+sin(uv.y*5.)*1.9+t*.2);float shine=pow(max(.5+.5*fold,0.),8.);vec3 satin=s.rgb*(.64+.36*shine)+vec3(.25,.19,.26)*shine;fragColor=vec4(clamp(mix(s.rgb,satin,strength),0.,1.),s.a);}
