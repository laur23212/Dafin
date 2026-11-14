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
uniform float size;
uniform float density;
uniform float intensity;
uniform float holdFrames;
uniform float seed;
void main(){

 vec4 c=texture(u_currentTexture,v_texCoord);vec2 uv=v_texCoord;
 vec2 grid=floor(uv*vec2(100.,56.)/max(.2,size));
 float f=floor(float(u_frameIndex)/max(1.,holdFrames));
 float n=hash21(grid+vec2(f*3.13,seed));
 vec2 center=(grid+.5)/ (vec2(100.,56.)/max(.2,size));
 float near=1.-smoothstep(.002,.014*size,length(uv-center));
 float spot=near*step(1.-density*.045,n)*intensity;
 fragColor=vec4(mix(c.rgb,vec3(.95),spot),c.a);

}
