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

void main(){vec4 s=inputAt(v_texCoord);vec3 c=s.rgb;vec3 t=vec3(smoothstep(.08,.97,c.r*.88+c.g*.09),smoothstep(.10,.92,c.g*.94+c.b*.12),smoothstep(.04,.97,c.b*.83+c.g*.07));t.r+=.11*pow(max(lum(c),0.),2.);t.b+=.085*(1.-lum(c));fragColor=vec4(clamp(mix(c,t,strength),0.,1.),s.a);}
