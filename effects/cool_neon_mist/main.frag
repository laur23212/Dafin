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
uniform float scale;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float a=exp(-length((uv-vec2(.03,.82))/vec2(.65,.5))*mix(5.,2.,scale));float b=exp(-length((uv-vec2(.87,.15))/vec2(.55,.8))*mix(5.,2.,scale));vec3 v=s.rgb+strength*(vec3(.1,.49,.66)*a*.53+vec3(.43,.17,.6)*b*.36);fragColor=vec4(clamp(v,0.,1.),s.a);}
