#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float sat(float x){return clamp(x,0.0,1.0);}
vec3 sat3(vec3 x){return clamp(x,vec3(0.0),vec3(1.0));}
float lum(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 px(){return 1.0/max(u_resolution,vec2(1.0));}
vec4 src(vec2 p){return texture(u_currentTexture,clamp(p,vec2(0.0001),vec2(0.9999)));}
float H(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}


uniform float density,steps,inkBlend,dotBias;
float bayer(vec2 p){int x=int(mod(floor(p.x),4.0));int y=int(mod(floor(p.y),4.0));int a[16]=int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);return (float(a[y*4+x])+.5)/16.0-.5;}
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);float sz=mix(1.5,6.0,density);vec2 grid=floor(uv*u_resolution/sz);vec2 su=(grid+0.5)*sz/max(u_resolution,vec2(1));vec3 x=src(su).rgb;float n=max(2.0,floor(steps+.5));float t=bayer(grid)*dotBias/(n-1.0);vec3 q=floor(clamp(x+vec3(t),0.0,.999)*(n-1.0)+0.5)/(n-1.0);vec3 pal=mix(q,vec3(lum(q))*vec3(.9,1.04,1.06),.15);fragColor=vec4(mix(q,pal,inkBlend),c.a);}

