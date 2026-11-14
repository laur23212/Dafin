#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float strength;
uniform float limit;

float sat(float x){return clamp(x,0.,1.);}
vec3 clip3(vec3 a){return clamp(a,vec3(0.),vec3(1.));}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
vec2 px(){return 1.0/max(u_resolution,vec2(1.));}
vec4 src(vec2 uv){return texture(u_currentTexture,clamp(uv,vec2(0.),vec2(1.)));}
float Y(vec2 uv){return lum(src(uv).rgb);}
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float edge(vec2 uv){vec2 d=px();float a=Y(uv+vec2(d.x,0.)),b=Y(uv-vec2(d.x,0.)),c=Y(uv+vec2(0.,d.y)),e=Y(uv-vec2(0.,d.y));return length(vec2(a-b,c-e))*0.75;}
vec3 blur4(vec2 uv,float radius){vec2 d=px()*radius;return (src(uv+vec2(d.x,0.)).rgb+src(uv-vec2(d.x,0.)).rgb+src(uv+vec2(0.,d.y)).rgb+src(uv-vec2(0.,d.y)).rgb)*.25;}
float hueGreen(vec3 c){return sat((c.g-max(c.r,c.b)) * 4.8 + 0.18);}
float hueBlue(vec3 c){return sat((c.b-c.r)*4.5)*sat((c.b-c.g*.78)*5.);}
float skinScore(vec3 c){float y=lum(c);float cb=(c.b-y)*.565;float cr=(c.r-y)*.713;float h=(1.-smoothstep(.09,.21,abs(cr-.13)))*(1.-smoothstep(.08,.18,abs(cb+.05)));return sat(h * smoothstep(.07,.20,y));}
float faceFall(vec2 uv,vec2 c,float r){vec2 p=(uv-c)*vec2(1.,u_resolution.x/max(1.,u_resolution.y));return 1.-smoothstep(r*.65,r,pow(dot(p,p),.5));}

void main(){

vec4 c=src(v_texCoord);vec3 avg=vec3(0.);float n=0.;
for(int j=0;j<4;j++)for(int i=0;i<4;i++){vec3 s=src((vec2(i,j)+.5)/4.).rgb;float satv=max(max(s.r,s.g),s.b)-min(min(s.r,s.g),s.b);float w=.3+.7*(1.-satv);avg+=s*w;n+=w;}
avg/=max(n,.001);float gray=dot(avg,vec3(.333333));vec3 gain=clamp(vec3(gray)/max(avg,vec3(.02)),vec3(1.-limit),vec3(1.+limit));
fragColor=vec4(clip3(mix(c.rgb,c.rgb*gain,strength)),c.a);

}
