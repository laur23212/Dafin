#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
float hashv(vec2 v){return fract(sin(dot(v,vec2(127.1,311.7)))*43758.5453);}
vec2 mirrorUV(vec2 uv){return 1.0-abs(mod(uv,2.0)-1.0);}
vec4 aAt(vec2 uv){return texture(u_fromTexture,mirrorUV(uv));}
vec4 bAt(vec2 uv){return texture(u_toTexture,mirrorUV(uv));}
vec4 blendAlpha(vec4 a,vec4 b,float t){float aa=mix(a.a,b.a,t);return vec4((a.rgb*a.a*(1.0-t)+b.rgb*b.a*t)/max(aa,0.00001),aa);}
uniform float character;

void main(){
vec2 uv=v_texCoord;
if(u_progress<=0.0){fragColor=texture(u_fromTexture,uv);return;}
if(u_progress>=0.985){fragColor=texture(u_toTexture,uv);return;}
float p=clamp(u_progress/0.985,0.0,1.0);
vec2 d=uv-.5;float ang=atan(d.y,d.x);float spiral=(ang+3.14159)/6.28318+length(d)*character*.7;float r=smoothstep(p-.04,p+.04,fract(spiral));float all=smoothstep(.82,1.,p);fragColor=blendAlpha(aAt(uv),bAt(uv),mix(r,1.,all));
fragColor=blendAlpha(fragColor,bAt(uv),smoothstep(.78,.94,p));
}
