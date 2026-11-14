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
uniform vec3 tint;
uniform float amount;
void main(){

 float p=sat(u_progress);float q=(p-.5)/.12; float fl=exp(-q*q)*amount; // pow() of a negative base is undefined in GLSL
 vec4 c=mix(a(v_texCoord),b(v_texCoord),smoothstep(.36,.64,p));
 float m=fl*exp(-pow((v_texCoord.x-.17)/.38,2.));
 fragColor=vec4(1.-(1.-c.rgb)*(1.-tint*m),c.a);

}
