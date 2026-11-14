#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
float r2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
vec2 mir(vec2 p){vec2 x=mod(p,2.0);return 1.0-abs(x-1.0);}
vec4 mixAlpha(vec4 a,vec4 b,float t){t=clamp(t,0.0,1.0);float oa=mix(a.a,b.a,t);vec3 col=(a.rgb*a.a*(1.0-t)+b.rgb*b.a*t)/max(oa,0.00001);return vec4(col,oa);}
uniform float turns;
uniform float soft;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
vec2 c=uv-vec2(.5);c.x*=u_resolution.x/max(1.,u_resolution.y);
 float r=length(c);float ang=atan(c.y,c.x)/6.2831853+.5;
 float spiral=r*1.1+ang*mix(.13,.46,turns);
 float t=p*1.7-.25;float v=1.-smoothstep(t-soft*.045-.002,t+soft*.045+.002,spiral);
 fragColor=mixAlpha(a,b,v);
}
