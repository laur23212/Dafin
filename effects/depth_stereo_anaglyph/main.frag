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


uniform float parallax,zeroPlane,balance;
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);if(u_hasDepth<.5){fragColor=c;return;}float d=driftDepthGuided(uv,u_currentTexture);float off=(d-zeroPlane)*parallax*.038;vec3 left=src(uv+vec2(off,0)).rgb;vec3 right=src(uv-vec2(off,0)).rgb;vec3 o=vec3(left.r,right.g,right.b);o=mix(o,vec3(lum(left),lum(right),lum(right)),.16*balance);fragColor=vec4(sat3(o),c.a);}

