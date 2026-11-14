#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
float r2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 mir(vec2 p){vec2 x=mod(p,2.0);return 1.0-abs(x-1.0);}
vec4 mixAlpha(vec4 a,vec4 b,float t){t=clamp(t,0.0,1.0);float oa=mix(a.a,b.a,t);vec3 col=(a.rgb*a.a*(1.0-t)+b.rgb*b.a*t)/max(oa,0.00001);return vec4(col,oa);}
uniform float columns;
uniform float stagger;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
float n=floor(mix(3.,11.,columns));float cell=floor(uv.x*n);
 float time=clamp((p*(1.+stagger*.8)-cell/max(n,1.)*stagger*.8),0.,1.);
 float turn=cos(time*3.14159265);
 float dx=(fract(uv.x*n)-.5)/max(abs(turn),.08)/n;
 vec2 warped=mir(vec2((cell+.5)/n+dx,uv.y));
 vec4 from=texture(u_fromTexture,warped);vec4 to=texture(u_toTexture,warped);
 float side=step(time,.5);
 fragColor=mixAlpha(from,to,1.-side);
 // At the first frame, force the complete original image.
 if(p<.005)fragColor=a;
}
