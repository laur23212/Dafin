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
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float patch=step(.48,rnd(floor((uv+vec2(uv.y*.5,0.))*mix(5.,27.,scale))));vec2 dx=px()*strength*15.;vec3 a=sampleRGB(uv+vec2(dx.x,0.));vec3 b=sampleRGB(uv-vec2(dx.x,0.));vec3 v=mix(s.rgb,vec3(a.r,s.rgb.g,b.b),patch);fragColor=vec4(v,s.a);}
