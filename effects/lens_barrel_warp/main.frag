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
uniform vec2 center;
uniform float barrel;
void main(){

 vec2 p=(v_texCoord-center)*vec2(u_resolution.x/max(1.,u_resolution.y),1.);
 float r2=dot(p,p);
 // Dafin: a positive (barrel) setting samples past the frame edge, which smeared the edge pixels
 // into the corners; zoom in just enough that the farthest corner from the centre stays inside.
 vec2 far=max(center,1.-center)*vec2(u_resolution.x/max(1.,u_resolution.y),1.);
 float fill=1.+max(barrel,0.)*dot(far,far);
 vec2 q=p*(1.+barrel*r2)/fill;
 q=q/vec2(u_resolution.x/max(1.,u_resolution.y),1.)+center;
 fragColor=safeSample(q);

}
