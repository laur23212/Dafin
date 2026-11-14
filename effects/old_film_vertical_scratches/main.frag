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
uniform float density;
uniform float width;
uniform float intensity;
uniform float holdFrames;
uniform float seed;
void main(){

 vec4 c=texture(u_currentTexture,v_texCoord);
 float frame=float(u_frameIndex);
 vec2 uv=v_texCoord;
 float x=hash21(vec2(floor(frame/max(1.,holdFrames)),seed))*.94+.03;
 float q=(uv.x-x)/max(.0003,width); float line=exp(-q*q); // pow() of a negative base is undefined in GLSL
 float isActive=step(1.-density,hash21(vec2(floor(frame/max(1.,holdFrames)),seed*2.67)));
 float trem=.78+.22*sin(uv.y*420.+frame*.2);
 vec3 outRgb=mix(c.rgb,vec3(1.),sat(isActive*line*intensity*trem));
 fragColor=vec4(outRgb,c.a);

}
