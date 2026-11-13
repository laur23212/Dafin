#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
const float PI = 3.141592653589793;
float sat(float v){return clamp(v,0.,1.);}
// Dafin: past the frame edge the picture is mirrored rather than clamped, which stretched the edge
// column into a pale band over up to a quarter of the frame while the clips slide.
vec2 mirrorUv(vec2 uv){vec2 m=mod(uv,2.);return clamp(mix(m,2.-m,step(1.,m)),vec2(0.001),vec2(0.999));}
vec4 a(vec2 uv){return texture(u_fromTexture,mirrorUv(uv));}
vec4 b(vec2 uv){return texture(u_toTexture,mirrorUv(uv));}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
uniform float shift;
uniform float blur;
void main(){

 float p=sat(u_progress);float ep=smoothstep(0.,1.,p);
 vec2 uv=v_texCoord;
 vec2 ua=uv+vec2(p*shift,0.);
 vec2 ub=uv-vec2((1.-p)*shift,0.);
 float blend=smoothstep(.44,.56,p);
 vec4 ca=vec4(0.),cb=vec4(0.);
 for(int i=0;i<5;i++){
 float k=(float(i)-2.)/2.*blur*sin(PI*p);
 ca+=a(ua+vec2(k,0.))/5.;cb+=b(ub+vec2(k,0.))/5.;
 }
 fragColor=mix(ca,cb,blend);

}
