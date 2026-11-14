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

void main(){vec2 uv=v_texCoord;vec4 s=inputAt(uv);float t=safeTime();float v=0.;for(int k=0;k<3;k++){float st=float(k)+1.;vec2 p=uv*vec2(19.*st,13.*st)+vec2(sin(t*.17*st),t*.31/st);vec2 cell=floor(p);vec2 f=fract(p)-vec2(.5);vec2 o=vec2(hashv(cell),hashv(cell+83.))-vec2(.5);float snow=1.-smoothstep(.03,.11,length(f-o*.44));v+=snow/(st*.9);}vec3 c=clamp(s.rgb+vec3(.8,.9,1.)*v*strength*.52,0.,1.);fragColor=vec4(c,s.a);}
