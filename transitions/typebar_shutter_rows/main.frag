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
 float row=floor(uv.y*18.);float n=hsh(vec2(row,3.));float front=(p-.12)*1.35+(n-.5)*.31*character;float r=1.-smoothstep(front-.04,front+.055,uv.x);fragColor=mixPM(A(uv),B(uv),r);
 fragColor=mixPM(fragColor,B(uv),smoothstep(.83,.955,p));
}
