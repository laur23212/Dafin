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
uniform float spacing;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);vec2 q=uv*vec2(mix(7.,19.,spacing),mix(5.,13.,spacing));vec2 g=fract(q)-.5;float r=length(g);float ring=exp(-abs(r-.22)*60.);float pit=1.-smoothstep(.08,.17,r);float shade=dot(normalize(g+vec2(.0001)),normalize(vec2(-.7,1.)));vec3 v=s.rgb+strength*(ring*vec3(.22,.25,.26)+pit*shade*.20)-strength*pit*.11;fragColor=vec4(clamp(v,0.,1.),s.a);}
