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
uniform float strength;
uniform float scale;
uniform float speed;
float haze(vec2 uv){float t=0.;float amp=.5;
 for(int i=0;i<4;i++){vec2 p=floor(uv),q=fract(uv);q=q*q*(3.-2.*q);
 float a=rand2(p),b=rand2(p+vec2(1.,0.)),c=rand2(p+vec2(0.,1.)),d=rand2(p+1.);
 t+=amp*mix(mix(a,b,q.x),mix(c,d,q.x),q.y);amp*=.5;uv=uv*2.13+3.1;}
 return t;}
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 vec2 p=uv*mix(2.0,10.0,scale)+vec2(tm()*speed*.095,-tm()*speed*.053);
 float clouds=smoothstep(.36,.70,haze(p));
 vec3 c=src.rgb*(1.-strength*.46*clouds);
 fragColor=vec4(c,src.a);}
