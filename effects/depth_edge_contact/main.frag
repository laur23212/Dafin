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


uniform float amount,spread,threshold;
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);if(u_hasDepth<.5){fragColor=c;return;}float d=driftDepthGuided(uv,u_currentTexture);vec2 pxl=px()*mix(2.0,15.0,spread);float edge=0.0;for(int i=0;i<8;i++){float a=float(i)*.785398163;float nearD=driftDepth(uv+vec2(cos(a),sin(a))*pxl);edge+=max(d-nearD,0.0);}float occ=smoothstep(mix(.10,.014,threshold),mix(.20,.05,threshold),edge)*amount*.50;fragColor=vec4(c.rgb*(1.0-occ),c.a);}

