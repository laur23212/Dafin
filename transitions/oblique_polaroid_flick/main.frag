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
uniform float rotation;
uniform float slide;
void main(){vec2 uv=v_texCoord;vec4 a=texture(u_fromTexture,uv);vec4 b=texture(u_toTexture,uv);
if(u_progress<=0.0){fragColor=a;return;}
if(u_progress>=0.955){fragColor=b;return;}
float t=clamp(u_progress/0.955,0.0,1.0);
float ang=t*(rotation-.5)*1.2;mat2 rot=mat2(cos(ang),-sin(ang),sin(ang),cos(ang));vec2 q=rot*(uv-.5)+.5+vec2(-t*slide*.95,t*.17);float inside=step(0.,q.x)*step(q.x,1.)*step(0.,q.y)*step(q.y,1.);float fade=1.-smoothstep(.34,.88,t);vec4 aa=texture(u_fromTexture,mir(q));fragColor=amix(b,aa,inside*fade);
}
