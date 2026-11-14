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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float t=safeTime();float fire=0.;for(int k=0;k<3;k++){float fk=float(k)+1.;vec2 p=uv*vec2(10.*fk,8.*fk);p+=vec2(sin(t*.37+fk),t*.24/fk);vec2 id=floor(p);vec2 f=fract(p)-.5;vec2 d=vec2(hashv(id+fk),hashv(id+fk+31.))-.5;float z=1.-smoothstep(.0,.16,length(f-d*.55));fire+=z*(.5+.5*sin(t*1.3+hashv(id)*8.))/fk;}vec3 c=clamp(s.rgb+vec3(.95,.72,.18)*fire*strength*.86,0.,1.);fragColor=vec4(c,s.a);}
