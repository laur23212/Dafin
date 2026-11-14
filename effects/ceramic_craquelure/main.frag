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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);vec2 p=uv*vec2(54.,36.);vec2 cell=floor(p);vec2 f=fract(p)-.5;float a=sin(dot(cell,vec2(12.1,7.8)))*.5+.5;float crack=min(abs(f.x+a*.19),abs(f.y-a*.15));float line=1.-smoothstep(.012,.043,crack);vec3 glaze=mix(s.rgb,vec3(.94,.92,.87)*(.52+.55*lum(s.rgb)),strength*.69);glaze*=1.-line*.24*strength;fragColor=vec4(clamp(glaze,0.,1.),s.a);}
