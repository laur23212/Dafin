#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mir(vec2 a){vec2 t=mod(a,2.0);return 1.0-abs(t-1.0);}
float lum(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
float rnd(vec2 x){return fract(sin(dot(x,vec2(127.1,311.7)))*43758.5453123);}
uniform float zoom;
uniform float frameSize;
uniform float tilt;

vec3 card(vec2 uv,vec2 ctr,vec2 extent,float angle,vec2 pan,vec3 base){
 vec2 p=uv-ctr;float sn=sin(angle),cs=cos(angle);p=vec2(cs*p.x+sn*p.y,-sn*p.x+cs*p.y);
 vec2 q=p/extent+.5;float outer=step(0.,q.x)*step(0.,q.y)*step(q.x,1.)*step(q.y,1.);
 float margin=mix(.014,.065,frameSize);float inner=step(margin,q.x)*step(margin,q.y)*step(q.x,1.-margin)*step(q.y,1.-margin);
 vec2 sampleUV=clamp(vec2(.5)+pan+(q-.5)*mix(.5,.82,zoom),0.,1.);
 vec3 col=texture(u_currentTexture,sampleUV).rgb;
 return mix(base,mix(vec3(.94,.93,.88),col,inner),outer);
}
void main(){vec4 s=texture(u_currentTexture,v_texCoord);
 vec3 c=s.rgb*.20+vec3(.045,.055,.060);
 c=card(v_texCoord,vec2(.29,.52),vec2(.29,.62),-.16*tilt,vec2(-.22,-.05),c);
 c=card(v_texCoord,vec2(.73,.54),vec2(.30,.67), .12*tilt,vec2(.19,.07),c);
 c=card(v_texCoord,vec2(.50,.48),vec2(.32,.76),-.04*tilt,vec2(.02,.0),c);
 fragColor=vec4(c,s.a);}
