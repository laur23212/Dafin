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
uniform float bands;
void main(){vec4 s=texture(u_currentTexture,v_texCoord);float y=Y(s.rgb);float k=floor(y*mix(3.,7.,bands))/mix(3.,7.,bands);vec3 palette=vec3(k*.96+.04,k*.62+.11,k*.68+.12);palette+=vec3(.08,-.06,-.04)*step(.5,k);fragColor=vec4(clamp(mix(s.rgb,palette,strength),0.,1.),s.a);}
