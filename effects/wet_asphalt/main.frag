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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float y=lum(s.rgb);float wet=smoothstep(.22,.79,y)*hashv(vec2(floor(uv.x*260.),floor(uv.y*52.)));float streak=smoothstep(.85,.98,wet);vec3 c=s.rgb*.72+vec3(.21,.27,.32)*wet+vec3(.47,.58,.72)*streak;fragColor=vec4(clamp(mix(s.rgb,c,strength),0.,1.),s.a);}
