#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform float sectors;
uniform float seamDarkening;
uniform vec2 pivot;
vec4 overMix(vec4 a,vec4 b,float w){float al=mix(a.a,b.a,w);vec3 pm=mix(a.rgb*a.a,b.rgb*b.a,w);return vec4(al>1e-6?pm/al:vec3(0),al);}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0),vec2(1));float p=clamp(u_progress,0.0,1.0);
 vec4 a=texture(u_fromTexture,uv),b=texture(u_toTexture,uv);
 if(p<=0.0){fragColor=a;return;}
 if(p>=.965){fragColor=b;return;}
 float t=smoothstep(0.0,.965,p);
 vec2 d=uv-pivot;
 float angle=atan(d.y,d.x);float u=(angle+3.141592653589793)/6.28318530718;
 float n=max(sectors,3.0);float sector=floor(u*n);float phase=fract(u*n);
 float start=sector/max(n,1.0)*.38;
 float open=smoothstep(start,start+.53,t);
 float leaf=1.0-smoothstep(open-.05,open+.08,phase);
 float shade=(1.0-smoothstep(.0,.05,phase))*(1.0-open)*seamDarkening;
 vec4 c=overMix(a,b,clamp(leaf,0.0,1.0));
 c.rgb*=1.0-shade;
 fragColor=c;
}