#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float strength;
uniform float threshold;
uniform float radiusPx;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);vec2 s=texel()*vec2(0.,radiusPx);
vec3 u=sampleAt(v_texCoord-s).rgb,d=sampleAt(v_texCoord+s).rgb;
vec3 u2=sampleAt(v_texCoord-2.*s).rgb,d2=sampleAt(v_texCoord+2.*s).rgb;
float y=luma(c.rgb);float y1=luma(u),y2=luma(d),two=luma((u+d)*.5);
float consistency=1.-smoothstep(threshold*.5,threshold*2.5,abs(y1-y2));
float comb=smoothstep(threshold,threshold*2.5,abs(y-two))*consistency;
float stable2=1.-smoothstep(threshold,threshold*4.,abs(y-luma((u2+d2)*.5)));
float amount=strength*comb*stable2;
fragColor=vec4(mix(c.rgb,(u+d)*.5,amount),c.a);
}
