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
uniform float weave;
uniform float speed;
uniform float seed;
uniform float mono;
uniform float contrast;
void main(){

 vec2 d=vec2(sin(u_time*speed*2.3+seed),cos(u_time*speed*1.69+seed))*(weave/1000.);
 vec2 uv=v_texCoord+d;
 vec4 s=safeSample(uv);
 float t=clamp((dot(s.rgb,vec3(.2126,.7152,.0722))-.5)*contrast+.5,0.,1.);
 fragColor=vec4(mix(s.rgb,vec3(t),mono),s.a);

}
