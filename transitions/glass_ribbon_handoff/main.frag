#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform float ribbonWidth;
uniform float refraction;
uniform float edgeContrast;
vec2 mirrorUV(vec2 p){return 1.0-abs(mod(p,2.0)-1.0);}
vec4 mixStraight(vec4 a,vec4 b,float w){float aa=mix(a.a,b.a,w);vec3 prem=mix(a.rgb*a.a,b.rgb*b.a,w);return vec4(aa>1e-6?prem/aa:vec3(0.0),aa);}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));float p=clamp(u_progress,0.0,1.0);
 vec4 a=texture(u_fromTexture,uv),b=texture(u_toTexture,uv);
 if(p<=0.0){fragColor=a;return;}
 if(p>=.965){fragColor=b;return;}
 float t=smoothstep(0.0,.965,p);
 float lead=mix(-ribbonWidth,1.0+ribbonWidth,t);
 float d=uv.x-lead;
 float center=smoothstep(-ribbonWidth*.5,ribbonWidth*.5,-d);
 float wave=sin(uv.y*19.0)*refraction*exp(-abs(d)/max(ribbonWidth,.02));
 vec2 q=mirrorUV(uv+vec2(wave,0.0));
 vec4 displaced=texture(u_toTexture,clamp(q,vec2(0.0),vec2(1.0)));
 vec4 front=mixStraight(a,displaced,center);
 float glass=exp(-abs(d)*12.0/max(ribbonWidth,.02))*edgeContrast*center*(1.0-center);
 front.rgb=mix(front.rgb,clamp(front.rgb+vec3(.14,.18,.22),0.0,1.0),glass);
 fragColor=front;
}