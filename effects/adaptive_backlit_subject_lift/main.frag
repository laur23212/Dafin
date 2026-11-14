#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float strength;
uniform float maxLift;
uniform float targetGap;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);if(u_hasClipMask<.5){fragColor=c;return;}
float sf=0.,sb=0.,wf=0.,wb=0.;
for(int j=0;j<4;j++)for(int i=0;i<6;i++){
 vec2 uv=(vec2(float(i)+.5,float(j)+.5))/vec2(6.,4.);
 float mask=driftMask(uv),y=luma(sampleAt(uv).rgb);
 sf+=y*mask;wf+=mask;sb+=y*(1.-mask);wb+=1.-mask;
}
float front=sf/max(wf,.0001),back=sb/max(wb,.0001);
float correction=clamp(back-front-targetGap,0.,maxLift)*strength;
float m=driftMask(v_texCoord);vec3 lifted=c.rgb+vec3(correction*(1.-luma(c.rgb)));
fragColor=vec4(safeRGB(mix(c.rgb,lifted,m)),c.a);
}
