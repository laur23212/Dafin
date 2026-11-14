#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
float satf(float x){return clamp(x,0.0,1.0);}
float rnd(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 mir(vec2 x){return 1.0-abs(mod(x,2.0)-1.0);}
vec4 amix(vec4 a,vec4 b,float t){t=clamp(t,0.0,1.0);float al=mix(a.a,b.a,t);return vec4((a.rgb*a.a*(1.-t)+b.rgb*b.a*t)/max(al,0.00001),al);}
uniform float flatten;
uniform float softness;
void main(){vec2 uv=v_texCoord;vec4 a=texture(u_fromTexture,uv);vec4 b=texture(u_toTexture,uv);
if(u_progress<=0.0){fragColor=a;return;}
if(u_progress>=0.955){fragColor=b;return;}
float t=clamp(u_progress/0.955,0.0,1.0);
vec2 q=(uv-.5)*vec2(1.,mix(1.2,3.,flatten));float r=length(q);float band=abs(r-(.42-t*.32));float k=1.-smoothstep(.04,.2+softness,band);k=max(k,smoothstep(.74,.94,t));fragColor=amix(a,b,k);
}
