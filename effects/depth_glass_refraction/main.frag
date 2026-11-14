#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mir(vec2 a){vec2 t=mod(a,2.0);return 1.0-abs(t-1.0);}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float rnd(vec2 x){return fract(sin(dot(x,vec2(127.1,311.7)))*43758.5453123);}
uniform float strength;
uniform float scale;
uniform float colorOffset;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);
 if(u_hasDepth<.5){fragColor=s;return;}
 vec3 n=driftNormal(v_texCoord,.22+scale*.5);
 vec2 shift=n.xy*strength*.042;
 vec2 uv=clamp(v_texCoord+shift,0.,1.);
 vec3 col=texture(u_currentTexture,uv).rgb;
 vec2 k=vec2(colorOffset*strength*.0035,0.);
 col.r=texture(u_currentTexture,clamp(uv+k,0.,1.)).r;
 col.b=texture(u_currentTexture,clamp(uv-k,0.,1.)).b;
 fragColor=vec4(col,s.a);}
