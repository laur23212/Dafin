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


uniform float wash,spread,edgeInk,paper;
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);vec2 d=px()*mix(1.0,5.0,spread);vec3 s=c.rgb*3.0;float total=3.0;for(int i=0;i<8;i++){float a=6.2831853*float(i)/8.0;vec2 q=uv+vec2(cos(a),sin(a))*d;vec3 t=src(q).rgb;float w=.75/(1.0+dot(t-c.rgb,t-c.rgb)*6.0);s+=t*w;total+=w;}vec3 pool=s/total;float e=length(vec2(lum(src(uv+vec2(d.x,0)).rgb)-lum(src(uv-vec2(d.x,0)).rgb),lum(src(uv+vec2(0,d.y)).rgb)-lum(src(uv-vec2(0,d.y)).rgb)));float grain=(H(floor(uv*u_resolution/3.0))-.5)*paper*.10;vec3 pigment=pool*(1.0-edgeInk*sat(e*2.4)*.3)+grain;fragColor=vec4(sat3(mix(c.rgb,pigment,wash)),c.a);}

