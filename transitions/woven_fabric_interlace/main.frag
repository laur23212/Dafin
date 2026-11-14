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
uniform float weave;
uniform float soft;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
vec2 px=uv*u_resolution;float n=mix(11.,34.,weave);
 vec2 q=uv*n;vec2 id=floor(q);
 float order=mod(id.x+id.y,2.);float local=fract(q.x);
 float threshold=p*1.9-.42+order*.26;
 float v=1.-smoothstep(threshold-soft*.03-.002,threshold+soft*.03+.002,local);
 fragColor=mixAlpha(a,b,v);
}
