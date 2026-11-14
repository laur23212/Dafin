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

void main(){vec4 s=inputAt(v_texCoord);vec3 c=s.rgb;float yy=lum(c);float spread=max(max(c.r,c.g),c.b)-min(min(c.r,c.g),c.b);float paper=smoothstep(.45,.83,yy)*(1.-smoothstep(.06,.21,spread));vec3 n=mix(c,vec3(yy),paper*strength);n=mix(n,vec3(1.),paper*strength*.12);fragColor=vec4(clamp(n,0.,1.),s.a);}
