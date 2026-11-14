#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
float sat(float x){return clamp(x,0.,1.);}
vec2 mirrorUv(vec2 x){return 1.0-abs(mod(x,2.0)-1.0);}
vec4 fromAt(vec2 uv){return texture(u_fromTexture,mirrorUv(uv));}
vec4 toAt(vec2 uv){return texture(u_toTexture,mirrorUv(uv));}
vec4 blendOver(vec4 f,vec4 b){float a=f.a+b.a*(1.-f.a);return vec4((f.rgb*f.a+b.rgb*b.a*(1.-f.a))/max(a,.000001),a);}

uniform float amplitude;
uniform float lineWidth;
uniform float travel;

void main(){
float t=sat(u_progress);
if(t<=0.000001){fragColor=fromAt(v_texCoord);return;}
if(t>=.99){fragColor=toAt(v_texCoord);return;}

vec2 uv=v_texCoord;float c=.5+sin(uv.x*9.0+1.5)*.13*amplitude+sin(uv.x*19.0)*.035*amplitude;float wave=uv.y-c;float lim=mix(-1.15,1.15,smoothstep(0.01,.97,t));float thr=wave+uv.x*.7+(.5-travel)*.2;float reveal=1.-smoothstep(lim-lineWidth,lim+lineWidth,thr);vec4 a=fromAt(uv),b=toAt(uv);fragColor=mix(a,b,reveal);

}
