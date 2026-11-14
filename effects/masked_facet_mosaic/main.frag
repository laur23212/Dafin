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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);if(u_hasClipMask<.5){fragColor=s;return;}float m=driftMask(uv);vec2 p=uv*vec2(33.,23.);vec2 q=floor(p+vec2(fract(p.y)*.45,0.));vec2 center=clamp((q+.5)/vec2(33.,23.),vec2(.001),vec2(.999));vec3 c=inputAt(center).rgb*(.85+.2*hashv(q));fragColor=vec4(clamp(mix(s.rgb,c,m*strength),0.,1.),s.a);}
