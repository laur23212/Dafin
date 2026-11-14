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
uniform float jump;
uniform float flash;
void main(){

 float p=sat(u_progress);float k=max(sin(PI*p),0.); // sin(PI) is a hair below 0 and pow() of a negative is undefined: black last frame
 vec2 uv=v_texCoord+vec2(sin(p*43.)*jump*k,cos(p*34.)*jump*k);
 vec4 s=mix(a(uv),b(uv),smoothstep(.45,.55,p));
 fragColor=vec4(mix(s.rgb,vec3(1.),flash*pow(k,8.)),s.a);

}
