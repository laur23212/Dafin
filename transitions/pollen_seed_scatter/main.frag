#version 330 core
in vec2 v_texCoord;out vec4 fragColor;
uniform sampler2D u_fromTexture;uniform sampler2D u_toTexture;
uniform vec2 u_resolution;uniform float u_progress;
float sat(float x){return clamp(x,0.,1.);}
float hsh(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 mir(vec2 u){return 1.-abs(mod(u,2.)-1.);}
vec4 A(vec2 u){return texture(u_fromTexture,mir(u));}
vec4 B(vec2 u){return texture(u_toTexture,mir(u));}
vec4 mixPM(vec4 a,vec4 b,float t){t=sat(t);float alpha=mix(a.a,b.a,t);vec3 prem=a.rgb*a.a*(1.-t)+b.rgb*b.a*t;return vec4(alpha>1e-6?prem/alpha:vec3(0.),alpha);}
uniform float character;
void main(){
 vec2 uv=v_texCoord;float raw=clamp(u_progress,0.,1.);
 if(raw<=0.0){fragColor=texture(u_fromTexture,uv);return;}
 if(raw>=.97){fragColor=texture(u_toTexture,uv);return;}
 float p=clamp(raw/.97,0.,1.);
 vec2 c=floor(uv*vec2(16.,10.));float n=hsh(c);vec2 local=fract(uv*vec2(16.,10.))-.5;float theta=n*6.283;vec2 rot=vec2(local.x*cos(theta)-local.y*sin(theta),local.x*sin(theta)+local.y*cos(theta));float r=length(rot*vec2(.7,1.6));float reveal=1.-smoothstep((p-.11)*.71+(n-.5)*.11,(p-.11)*.71+(n-.5)*.11+.065,r);fragColor=mixPM(A(uv),B(uv),reveal);
 fragColor=mixPM(fragColor,B(uv),smoothstep(.83,.955,p));
}
