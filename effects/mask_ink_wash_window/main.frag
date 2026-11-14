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


uniform float wash,darkInk,softness;
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);if(u_hasClipMask<.5){fragColor=c;return;}vec2 d=px()*mix(1.,5.,softness);vec3 x=(src(uv+vec2(d.x,0)).rgb+src(uv-vec2(d.x,0)).rgb+src(uv+vec2(0,d.y)).rgb+src(uv-vec2(0,d.y)).rgb)*.25;float e=abs(lum(src(uv+vec2(d.x,0)).rgb)-lum(src(uv-vec2(d.x,0)).rgb))+abs(lum(src(uv+vec2(0,d.y)).rgb)-lum(src(uv-vec2(0,d.y)).rgb));vec3 ink=mix(x,vec3(lum(x)*.86),.35)*(1.0-sat(e*darkInk*2.5)*.56);float m=driftMask(uv);fragColor=vec4(mix(c.rgb,ink,sat(m*wash)),c.a);}

