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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float t=safeTime();float h=sin(uv.y*7.+sin(uv.x*7.+t*.1)*1.4-t*.21);float p=smoothstep(.03,.89,.5+.5*h);vec3 ink=mix(vec3(.12,.58,.65),vec3(.79,.35,.58),p);vec3 c=clamp(s.rgb*(1.-strength*.12)+ink*strength*.23*p,0.,1.);fragColor=vec4(c,s.a);}
