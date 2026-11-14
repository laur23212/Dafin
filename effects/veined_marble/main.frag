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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float xx=uv.x*14.+sin(uv.y*26.)*.8+sin(uv.x*25.+uv.y*12.)*.17;float vein=1.-smoothstep(.02,.13,abs(sin(xx)));float yy=lum(s.rgb);vec3 mineral=mix(vec3(.21,.27,.30),vec3(.91,.88,.81),yy);mineral*=1.-vein*.56;fragColor=vec4(mix(s.rgb,mineral,strength),s.a);}
