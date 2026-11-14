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
uniform float scale;

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float rr=length((uv-vec2(.55,.48))*vec2(u_resolution.x/max(u_resolution.y,1.),1.));float bands=sin(rr*rr*140.*scale)*.5+.5;float envelope=1.-smoothstep(.15,.85,rr);vec3 tint=vec3(.7+.3*bands,.7+.23*sin(rr*rr*152.*scale+2.),.65+.33*sin(rr*rr*142.*scale+4.));vec3 c=mix(s.rgb,tint*lum(s.rgb),envelope*strength*.72);fragColor=vec4(clamp(c,0.,1.),s.a);}
