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

uniform float branch;
uniform float softness;
uniform float seed;

void main(){
float t=sat(u_progress);
if(t<=0.000001){fragColor=fromAt(v_texCoord);return;}
if(t>=.99){fragColor=toAt(v_texCoord);return;}

vec2 uv=v_texCoord;float x=uv.x;float y=uv.y;float facet=abs(fract(y*11.+x*3.+seed*9.)-.5);float vein=sin(y*34.+x*19.+seed*6.)*sin(x*8.-y*22.);float border=x+.18*branch*facet-.15*branch*vein;float edge=smoothstep(t*1.45-.05-softness,t*1.45+.05+softness,border);float reveal=1.-edge;fragColor=mix(fromAt(uv),toAt(uv),reveal);

}
