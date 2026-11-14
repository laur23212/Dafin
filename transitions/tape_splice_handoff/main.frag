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
uniform float diagonal;
uniform float tapeWidth;
uniform float tapeShade;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
float line=(p*1.6-.28)+(uv.y-.5)*(diagonal-.5)*.68;
 float w=mix(.007,.035,tapeWidth);
 float reveal=1.-smoothstep(line-w,line+w,uv.x);
 vec4 result=alphaMix(a,b,reveal);
 float seam=1.-smoothstep(.0,w+.008,abs(uv.x-line));
 float seamAlpha=seam*(1.-smoothstep(.89,.97,p))*(1.-smoothstep(.0,.11,p));
 vec3 seamInk=vec3(.64,.60,.51)*(1.-tapeShade*.38);
 result.rgb=mix(result.rgb,seamInk,seamAlpha*tapeShade*.58);
 fragColor=result;
}
