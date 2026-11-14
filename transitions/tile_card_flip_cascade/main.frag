#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 mirrored(vec2 v){return 1.0-abs(mod(v,2.0)-1.0);}
vec4 overStraight(vec4 a,vec4 b){float outA=a.a+b.a*(1.0-a.a);return vec4((a.rgb*a.a+b.rgb*b.a*(1.0-a.a))/max(outA,0.00001),outA);}
vec4 alphaMix(vec4 a,vec4 b,float t){t=clamp(t,0.0,1.0);float outA=mix(a.a,b.a,t);return vec4((a.rgb*a.a*(1.0-t)+b.rgb*b.a*t)/max(outA,0.00001),outA);}
uniform float tiles;
uniform float cascade;
uniform float hinge;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
vec2 count=vec2(mix(5.,12.,tiles),mix(4.,8.,tiles));
 vec2 cellUV=uv*count;vec2 tileId=floor(cellUV);vec2 local=fract(cellUV);
 float tileOrder=(tileId.x/max(count.x-1.,1.)+tileId.y/max(count.y-1.,1.))*.5;
 float duration=mix(.19,.47,cascade);
 float localP=clamp((p-tileOrder*duration)/max(.01,1.-duration),0.,1.);
 float flap=abs(cos(localP*3.14159265));
 float side=step(.5,localP);
 float dx=(local.x-.5)/max(.08,flap*(1.-.3*hinge));
 vec2 mapped=mirrored((tileId+vec2(.5+dx,local.y))/count);
 vec4 frame=mix(texture(u_fromTexture,mapped),texture(u_toTexture,mapped),side);
 float face=step(abs(dx),.5);
 vec4 behind=texture(u_toTexture,uv);
 float shade=mix(1.-hinge*.29,1.,flap);
 frame.rgb*=shade;
 fragColor=alphaMix(behind,frame,face);
}
