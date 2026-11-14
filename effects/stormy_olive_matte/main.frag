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
uniform float matte;
void main(){vec4 s=texture(u_currentTexture,v_texCoord);float y=Y(s.rgb);vec3 olive=vec3(.31,.39,.22);vec3 v=mix(s.rgb,vec3(y)*.65+olive*.35,.68);v=mix(v,vec3(.07,.10,.07),smoothstep(.35,0.,y)*.28);v=mix(v,vec3(.05,.08,.03),matte*.12);fragColor=vec4(mix(s.rgb,clamp(v,0.,1.),strength),s.a);}
