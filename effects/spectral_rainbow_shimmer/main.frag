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
uniform float speed;
uniform float width;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float line=uv.y-uv.x*.22-(.2+.6*(.5+.5*sin(ts()*speed*.8)));float k=exp(-abs(line)*mix(75.,10.,width));float a=uv.x*12.+ts()*speed*2.;vec3 rgb=.5+.5*cos(a+vec3(0.,2.0944,4.1888));fragColor=vec4(clamp(s.rgb+rgb*k*strength*.5,0.,1.),s.a);}
