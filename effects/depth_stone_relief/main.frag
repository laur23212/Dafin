#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float hashv(vec2 v){return fract(sin(dot(v,vec2(127.1,311.7)))*43758.5453);}
vec2 reflectUV(vec2 uv){return 1.0-abs(mod(uv,2.0)-1.0);}
vec4 inputAt(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.001),vec2(.999)));}
float safeTime(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);if(u_hasDepth<.5){fragColor=s;return;}vec3 n=driftNormal(uv,1.4);float light=dot(n,normalize(vec3(-.48,-.58,.68)))*.5+.5;float d=driftDepth(uv);vec3 stone=mix(vec3(.15,.18,.17),vec3(.77,.73,.62),light)*(.78+.22*d);fragColor=vec4(mix(s.rgb,stone,strength),s.a);}
