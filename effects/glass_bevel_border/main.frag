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
uniform float width;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);float d=min(min(uv.x,1.-uv.x),min(uv.y,1.-uv.y));float band=1.-smoothstep(width-.018,width+.009,d);float rim=exp(-abs(d-width)*130.);vec3 tint=vec3(.45,.63,.73);vec3 v=mix(s.rgb*.74+tint*.24,s.rgb,smoothstep(0.,width,d));v+=rim*vec3(.33,.37,.41)*strength*.33;fragColor=vec4(clamp(mix(s.rgb,v,strength*band),0.,1.),s.a);}
