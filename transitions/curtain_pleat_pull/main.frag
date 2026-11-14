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
uniform float pleats;
uniform float shade;
void main(){
 vec2 uv=v_texCoord;
 vec4 a=texture(u_fromTexture,uv);
 vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.0,1.0);
float fold=.035*sin(uv.y*mix(22.,57.,pleats)+uv.x*2.8);
 float edge=mix(-.2,1.3,p)+fold*(1.-p);
 float moving=1.-smoothstep(edge-.018,edge+.018,uv.x);
 vec2 wrapped=mir(vec2(uv.x+(p*.25)*moving,uv.y+fold*.12));
 vec4 outgoing=texture(u_fromTexture,wrapped);
 float light=1.-shade*.12*(.5+.5*sin(uv.y*mix(22.,57.,pleats)));
 outgoing.rgb*=light;
 fragColor=mixAlpha(outgoing,b,moving);
}
