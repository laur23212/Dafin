#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float u_frameRate;
const float PI = 3.141592653589793;
float sat(float v){return clamp(v,0.,1.);}
vec3 sat3(vec3 v){return clamp(v,vec3(0.),vec3(1.));}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 aspectUv(vec2 uv){return (uv-0.5)*vec2(u_resolution.x/max(1.,u_resolution.y),1.);}
vec4 safeSample(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.001),vec2(0.999)));}
uniform float amount;
uniform float speed;
uniform float seed;
uniform float crop;
void main(){

 float time=float(u_frameIndex)/max(1.,u_frameRate);
 float t=time*max(.1,speed);
 float ix=sin(t*2.19+seed*1.77)+0.42*sin(t*5.37+2.1+seed);
 float iy=cos(t*1.93+seed*1.43)+0.33*sin(t*4.71+0.4);
 float mag=amount/100.;
 vec2 drift=vec2(ix,iy)*mag;
 vec2 uv=(v_texCoord-.5)/(1.+crop/100.)+.5+drift;
 fragColor=safeSample(uv);

}
