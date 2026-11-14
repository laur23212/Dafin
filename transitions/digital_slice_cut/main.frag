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
uniform float slices;
uniform float tear;
void main(){

 float p=sat(u_progress);float row=floor(v_texCoord.y*slices);
 float n=hash21(vec2(row,3.));float jump=(n-.5)*tear*sin(PI*p);
 vec2 uv=v_texCoord+vec2(jump,0.);
 float edge=smoothstep(.3,.7,p+(n-.5)*.36);
 fragColor=mix(a(uv),b(uv),edge);

}
