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
uniform float punch;
void main(){vec4 s=texture(u_currentTexture,v_texCoord);float y=Y(s.rgb);float red=satf((s.rgb.r-max(s.rgb.g,s.rgb.b))*.9+.43);vec3 v=mix(s.rgb*vec3(.76,.61,.82)+vec3(.12,.015,.12)*(1.-y),s.rgb*vec3(1.19,.71,.87)+vec3(.18,0.,.05),red*punch);fragColor=vec4(clamp(mix(s.rgb,v,strength),0.,1.),s.a);}
