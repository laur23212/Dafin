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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);vec2 q=(uv-.5)*vec2(u_resolution.x/max(u_resolution.y,1.),1.);float r=smoothstep(.15,.82,length(q));vec2 e=vec2(.007,0.)*r*strength;vec3 b=(inputAt(uv+e).rgb+inputAt(uv-e).rgb+inputAt(uv+e*2.).rgb+inputAt(uv-e*2.).rgb)*.25;fragColor=vec4(mix(s.rgb,b,r*strength),s.a);}
