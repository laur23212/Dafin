#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mirrorUV(vec2 v){vec2 t=mod(v,2.0);return 1.0-abs(t-1.0);}
float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
float rand2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float tm(){return float(u_frameIndex)/max(1.0,u_frameRate);}
uniform float density;
uniform float lens;
uniform float contrast;
uniform float drift;
void main(){vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,uv);
 vec2 aspect=vec2(u_resolution.x/max(1.,u_resolution.y),1.);
 vec2 p=uv*aspect*mix(19.,42.,density);p.y+=tm()*drift*.6;
 vec2 g=floor(p);vec2 h=vec2(rand2(g+.7),rand2(g+13.));
 vec2 q=fract(p)-(.15+.7*h);q.x*=.85;
 float r=length(q);float edge=1.-smoothstep(.12,.35,r);
 vec2 dir=q/max(r,.0001);
 vec2 sh=dir*edge*edge*lens*.014;
 vec3 refr=texture(u_currentTexture,clamp(uv+sh,0.,1.)).rgb;
 float spec=exp(-125.*dot(q-vec2(-.075,-.08),q-vec2(-.075,-.08)))*contrast*.28;
 fragColor=vec4(clamp(mix(s.rgb,refr,edge*.82)+vec3(spec),0.,1.),s.a);}
