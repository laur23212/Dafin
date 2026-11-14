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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float y=lum(s.rgb);float strata=sin(uv.y*95.+sin(uv.x*17.)*2.)*.5+.5;float grain=hashv(floor(uv*u_resolution*.56));vec3 stone=mix(vec3(.31,.16,.09),vec3(.87,.69,.43),smoothstep(.08,.94,y));stone*=.79+.21*strata+.09*(grain-.5);fragColor=vec4(clamp(mix(s.rgb,stone,strength),0.,1.),s.a);}
