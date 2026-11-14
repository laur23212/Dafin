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
uniform float focus;
uniform float width;

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float d=abs(uv.y-focus);float blur=smoothstep(width*.45,width,d)*strength;vec2 k=vec2(.0038*blur,.004*blur);vec3 b=(s.rgb*2.+inputAt(uv+k).rgb+inputAt(uv-k).rgb+inputAt(uv+vec2(k.x,-k.y)).rgb+inputAt(uv+vec2(-k.x,k.y)).rgb)/6.;fragColor=vec4(mix(s.rgb,b,blur),s.a);}
