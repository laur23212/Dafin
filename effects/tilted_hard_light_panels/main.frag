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
uniform float angle;
uniform float spread;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float x=uv.x+(uv.y-.5)*(angle-.5)*1.3;float a=1.-smoothstep(spread*.16,spread*.32,abs(x-.3));float b=1.-smoothstep(spread*.10,spread*.26,abs(x-.74));vec3 v=s.rgb+(vec3(.51,.34,.19)*a+vec3(.22,.40,.58)*b)*strength*.54;fragColor=vec4(clamp(v,0.,1.),s.a);}
