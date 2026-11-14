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
uniform float rotation;
uniform float departure;
uniform float soft;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
float q=smoothstep(0.,1.,p);
 float theta=q*mix(1.1,3.7,rotation);
 vec2 c=uv-.5;
 mat2 r=mat2(cos(theta),-sin(theta),sin(theta),cos(theta));
 vec2 move=vec2(q*departure*.85,-q*departure*.18);
 vec2 inv=r*(c+move)/max(.3,1.-q*.45)+.5;
 float inside=smoothstep(0.,.013+soft*.018,inv.x)*smoothstep(0.,.013+soft*.018,inv.y);
 inside*=1.-smoothstep(.987-soft*.019,1.,inv.x);
 inside*=1.-smoothstep(.987-soft*.019,1.,inv.y);
 vec4 turned=texture(u_fromTexture,mirrored(inv));
 turned.a*=inside;
 fragColor=overStraight(turned,b);
}
