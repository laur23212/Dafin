#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float softness;
uniform float priority;

float sat(float a){return clamp(a,0.,1.);}
vec3 clip3(vec3 a){return clamp(a,vec3(0.),vec3(1.));}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float reveal(float rank,float p,float feather){float f=max(.001,feather);return smoothstep(rank-f,rank+f,p*(1.+2.*f)-f);}
vec4 blend(vec4 a,vec4 b,float t){float alpha=mix(a.a,b.a,t);vec3 rgb=mix(a.rgb*a.a,b.rgb*b.a,t);return vec4(alpha>1e-5?rgb/alpha:vec3(0.),alpha);}
float edgeTo(vec2 uv){vec2 d=1./max(vec2(1.),u_resolution);vec3 r=texture(u_toTexture,clamp(uv+vec2(d.x,0.),vec2(0.),vec2(1.))).rgb;vec3 l=texture(u_toTexture,clamp(uv-vec2(d.x,0.),vec2(0.),vec2(1.))).rgb;vec3 u=texture(u_toTexture,clamp(uv+vec2(0.,d.y),vec2(0.),vec2(1.))).rgb;vec3 dn=texture(u_toTexture,clamp(uv-vec2(0.,d.y),vec2(0.),vec2(1.))).rgb;return length(vec2(lum(r)-lum(l),lum(u)-lum(dn)))*.8;}

void main(){

vec4 a=texture(u_fromTexture,v_texCoord),b=texture(u_toTexture,v_texCoord);float detail=edgeTo(v_texCoord);float rank=mix(.5,1.-smoothstep(.01,.25,detail),priority);fragColor=blend(a,b,reveal(rank,u_progress,softness));

}
