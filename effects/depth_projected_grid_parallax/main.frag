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
uniform float density;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);if(u_hasDepth<.5){fragColor=s;return;}float z=driftDepthGuided(uv,u_currentTexture);vec2 q=(uv+vec2(z*.028,-z*.018))*mix(13.,55.,density);vec2 l=abs(fract(q)-.5);float line=1.-smoothstep(.45,.49,max(l.x,l.y));vec3 v=s.rgb+vec3(.12,.51,.78)*line*z*strength*.56;fragColor=vec4(clamp(v,0.,1.),s.a);}
