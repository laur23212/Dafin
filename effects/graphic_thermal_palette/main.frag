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
uniform float amount;
uniform float contrast;
uniform float smoothness;
vec3 heat(float x){x=clamp(x,0.,1.);vec3 dark=vec3(.08,.03,.25);vec3 blue=vec3(.07,.22,.69);vec3 cyan=vec3(.05,.75,.86);vec3 gold=vec3(1.,.82,.18);vec3 red=vec3(.95,.15,.06);
 float blur=mix(.018,.12,smoothness);
 vec3 c=mix(dark,blue,smoothstep(.08-blur,.22+blur,x));
 c=mix(c,cyan,smoothstep(.26-blur,.44+blur,x));
 c=mix(c,gold,smoothstep(.48-blur,.68+blur,x));
 return mix(c,red,smoothstep(.75-blur,.92+blur,x));}
void main(){vec4 src=texture(u_currentTexture,v_texCoord);float y=luma(src.rgb);
 float l=clamp((y-.5)*(1.+contrast)+.5,0.,1.);
 fragColor=vec4(mix(src.rgb,heat(l),amount),src.a);}
