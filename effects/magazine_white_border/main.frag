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
uniform float width;
uniform float cream;
uniform float shadow;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float edge=min(min(uv.x,1.-uv.x),min(uv.y,1.-uv.y));float outer=1.-smoothstep(width-.002,width+.002,edge);float shade=exp(-abs(edge-width)*120.)*shadow;vec3 paper=mix(vec3(.99),vec3(.95,.92,.85),cream);fragColor=vec4(mix(s.rgb*(1.-shade*.28),paper,outer),s.a);}
