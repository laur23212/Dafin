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
uniform float border;
uniform float zoom;
uniform float matte;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 vec2 pos=uv*vec2(3.,2.);vec2 id=floor(pos);vec2 cell=fract(pos);
 float edge=mix(.025,.095,border);
 float inside=step(edge,cell.x)*step(edge,cell.y)*step(cell.x,1.-edge)*step(cell.y,1.-edge);
 vec2 center=vec2(.5)+vec2((id.x-1.)*.16,(id.y-.5)*.23);
 vec2 su=clamp(center+(cell-.5)*mix(.46,.70,1.-zoom)*vec2(0.6666667,1.),0.,1.);
 vec3 frame=texture(u_currentTexture,su).rgb;
 vec3 under=mix(vec3(.052,.059,.066),vec3(.17,.15,.12),matte);
 fragColor=vec4(mix(under,frame,inside),src.a);}
