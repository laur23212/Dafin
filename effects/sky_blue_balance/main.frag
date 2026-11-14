#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float sat(float x){return clamp(x,0.0,1.0);}
vec3 sat3(vec3 x){return clamp(x,vec3(0.0),vec3(1.0));}
float lum(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 px(){return 1.0/max(u_resolution,vec2(1.0));}
vec4 src(vec2 p){return texture(u_currentTexture,clamp(p,vec2(0.0001),vec2(0.9999)));}
float H(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}


uniform float skyShift,skyStrength,protect;
void main(){vec4 c=src(v_texCoord);vec3 x=c.rgb;float blue=sat((x.b-max(x.r,x.g)*.92)*3.2+.25);float cy=sat((x.g-x.r)*3.0+.1);float mask=sat(max(blue,cy*.55))*(.25+.75*smoothstep(.25,.65,lum(x)));float neutral=1.-smoothstep(.03,.22,max(max(x.r,x.g),x.b)-min(min(x.r,x.g),x.b));mask*=1.-protect*neutral;vec3 target=vec3(x.r*.75,x.g*(1.06+skyShift*.12),x.b*(1.16-skyShift*.28));fragColor=vec4(sat3(mix(x,target,mask*skyStrength)),c.a);}

