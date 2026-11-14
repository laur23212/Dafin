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
uniform float detail;
uniform float depthCut;
uniform float radius;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 if(u_hasDepth<.5){fragColor=src;return;}
 float dep=driftDepthGuided(uv,u_currentTexture);
 float fg=smoothstep(depthCut-.15,depthCut+.17,dep);
 vec2 px=vec2(1.)/max(u_resolution,vec2(1.))*mix(1.,3.,radius);
 vec3 smoothc=src.rgb*.36;
 smoothc+=texture(u_currentTexture,clamp(uv+vec2(px.x,0.),0.,1.)).rgb*.16;
 smoothc+=texture(u_currentTexture,clamp(uv-vec2(px.x,0.),0.,1.)).rgb*.16;
 smoothc+=texture(u_currentTexture,clamp(uv+vec2(0.,px.y),0.,1.)).rgb*.16;
 smoothc+=texture(u_currentTexture,clamp(uv-vec2(0.,px.y),0.,1.)).rgb*.16;
 vec3 sharpened=clamp(src.rgb+detail*fg*(src.rgb-smoothc)*2.,0.,1.);
 fragColor=vec4(sharpened,src.a);}
