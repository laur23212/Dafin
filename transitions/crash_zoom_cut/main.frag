#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
const float PI = 3.141592653589793;
float sat(float v){return clamp(v,0.,1.);}
vec4 a(vec2 uv){return texture(u_fromTexture,clamp(uv,vec2(0.001),vec2(0.999)));}
vec4 b(vec2 uv){return texture(u_toTexture,clamp(uv,vec2(0.001),vec2(0.999)));}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
uniform float zoom;
uniform float blur;
void main(){

 float p=sat(u_progress);float e=smoothstep(0.,1.,p);
 bool second=p>=.5; float t=second?(1.-p)*2.:p*2.;
 vec2 uv=.5+(v_texCoord-.5)/(1.+zoom*t);
 vec4 s=vec4(0.);
 for(int k=0;k<7;k++){
 float offs=(float(k)-3.)/3.*blur*t;
 vec2 q=clamp(mix(uv,vec2(.5),offs),0.,1.);
 s+=(second?b(q):a(q))/7.;
 }
 fragColor=s;

}
