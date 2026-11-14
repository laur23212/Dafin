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
uniform float density;
uniform float retainColor;
uniform float lift;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);float Y=lum(s.rgb);
 vec3 silver=vec3(smoothstep(.035,.96,Y));
 vec3 contrasted=clamp((s.rgb-.5)*(1.+density*.72)+.5,0.,1.);
 vec3 outc=mix(contrasted,silver,density*(1.-retainColor));
 outc=mix(outc,s.rgb,retainColor*.32);
 outc=mix(outc,vec3(smoothstep(0.,.86,Y)),lift*.16);
 fragColor=vec4(clamp(outc,0.,1.),s.a);}
