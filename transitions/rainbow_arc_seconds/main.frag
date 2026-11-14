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
 vec2 q=uv-vec2(.5,1.);float radius=length(q);float front=1.2-p*1.4;float r=smoothstep(front-.07,front+.035,radius);vec4 a=A(uv);a.rgb=clamp(a.rgb+vec3(.06,.09,.16)*exp(-abs(radius-front)*38.)*character,0.,1.);fragColor=mixPM(a,B(uv),r);
 fragColor=mixPM(fragColor,B(uv),smoothstep(.83,.955,p));
}
