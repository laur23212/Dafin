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
uniform float rate;
uniform float density;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);vec2 q=(uv-.5)*vec2(u_resolution.x/max(u_resolution.y,1.),1.);float a=atan(q.y,q.x);float r=length(q);float phase=a+ts()*rate*.5;float arcs=smoothstep(.97,1.,sin(phase*mix(4.,16.,density)+r*18.));float light=arcs*smoothstep(.08,.3,r)*(1.-smoothstep(.7,1.,r));fragColor=vec4(clamp(s.rgb+vec3(.15,.7,.95)*light*strength*.65,0.,1.),s.a);}
