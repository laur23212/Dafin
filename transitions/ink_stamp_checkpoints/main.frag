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
uniform float scale;
uniform float stagger;
void main(){vec2 uv=v_texCoord;vec4 a=texture(u_fromTexture,uv);vec4 b=texture(u_toTexture,uv);
if(u_progress<=0.0){fragColor=a;return;}
if(u_progress>=0.955){fragColor=b;return;}
float t=clamp(u_progress/0.955,0.0,1.0);
vec2 q=uv*vec2(mix(5.,15.,scale),mix(4.,9.,scale));vec2 id=floor(q);float delay=.43*rnd(id+vec2(id.y*.21,id.x*.17))*stagger;float k=smoothstep(delay+.03,delay+.38,t+fract(q.x)*.14);fragColor=amix(a,b,k);
}
