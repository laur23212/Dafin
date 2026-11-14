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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);vec2 p=uv*vec2(26.,16.);vec2 f=fract(p);vec2 id=floor(p);float h=hashv(id);float wire=1.-smoothstep(.035,.09,min(abs(f.x-.5),abs(f.y-.5)));float node=1.-smoothstep(.03,.115,length(f-.5));float pattern=wire*(.3+.7*step(.42,h))+node;vec3 c=mix(s.rgb*.43+vec3(.02,.055,.08),vec3(.2,.86,.64),clamp(pattern,0.,1.));fragColor=vec4(clamp(mix(s.rgb,c,strength),0.,1.),s.a);}
