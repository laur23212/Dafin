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


uniform float threshold,amount,highlight;
void main(){vec4 c=src(v_texCoord);float y=lum(c.rgb);float m=max(max(c.r,c.g),c.b);float lo=min(min(c.r,c.g),c.b);float span=m-lo;float over=max(span-threshold,0.0);float compress=over*over/(over+.18);float k=clamp(amount*compress/(span+.0001),0.,.9)*mix(.55,1.0,smoothstep(.3,.8,y)*highlight);vec3 o=mix(c.rgb,vec3(y),k);fragColor=vec4(sat3(o),c.a);}

