#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float Y(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float satf(float x){return clamp(x,0.0,1.0);}
vec2 mir(vec2 x){return 1.0-abs(mod(x,2.0)-1.0);}
float rnd(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float ts(){return float(u_frameIndex)/max(u_frameRate,1.0);}
vec2 px(){return 1.0/max(u_resolution,vec2(1.0));}
vec3 sampleRGB(vec2 uv){return texture(u_currentTexture,clamp(uv,0.0,1.0)).rgb;}
uniform float strength;
uniform float geometry;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);vec2 g=uv*mix(4.,14.,geometry);vec2 f=fract(g);float tri=step(f.y,f.x*.8+.12);float h=rnd(floor(g));vec3 ink=mix(vec3(.84,.23,.31),vec3(.12,.72,.64),tri);ink=mix(ink,vec3(.99,.75,.22),step(.66,h)*.65);vec3 v=mix(s.rgb,ink*s.rgb*.55+ink*.38,strength*.59);fragColor=vec4(clamp(v,0.,1.),s.a);}
