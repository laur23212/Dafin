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
uniform float levels;
uniform float inkStrength;
uniform float dotScale;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);vec3 c=s.rgb;
 float n=mix(7.,3.,levels);vec3 poster=floor(c*n+.5)/n;
 float y=lum(poster);vec2 pixel=v_texCoord*vec2(1080.*u_resolution.x/max(u_resolution.y,1.),1080.);
 float grain=(rnd(floor(pixel/12.))-0.5)*(.06*inkStrength);
 float dot=sin(pixel.x*mix(.12,.47,dotScale))*sin(pixel.y*mix(.12,.47,dotScale));
 vec3 o=poster+grain-dot*inkStrength*.026;
 o=mix(o,o*vec3(1.,.98,.94),inkStrength);
 fragColor=vec4(clamp(o,0.,1.),s.a);}
