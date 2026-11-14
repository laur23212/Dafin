#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mirrorUV(vec2 v){vec2 t=mod(v,2.0);return 1.0-abs(t-1.0);}
float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
float rand2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float tm(){return float(u_frameIndex)/max(1.0,u_frameRate);}
uniform float wrap;
uniform float width;
uniform float warm;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 if(u_hasClipMask<.5){fragColor=src;return;}
 float m=clamp(driftMask(uv),0.,1.);
 vec2 px=vec2(1.)/max(u_resolution,vec2(1.));float d=m;
 for(int i=0;i<8;i++){
 float angle=6.2831853*float(i)/8.;
 vec2 off=vec2(cos(angle),sin(angle))*px*(2.+width*4.);
 d=min(d,clamp(driftMask(clamp(uv+off,0.,1.)),0.,1.));}
 float band=clamp(m-d,0.,1.);
 vec3 outside=texture(u_currentTexture,clamp(uv+vec2(0.,2.+width*3.)*px,0.,1.)).rgb;
 vec3 reflected=mix(outside,outside*vec3(1.0,.89,.70),warm*.6);
 fragColor=vec4(mix(src.rgb,reflected,band*wrap*.76),src.a);}
