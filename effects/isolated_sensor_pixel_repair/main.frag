#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float threshold;
uniform float radiusPx;
uniform float strength;
uniform float repairDark;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);vec2 t=texel()*radiusPx;
vec3 a=sampleAt(v_texCoord+vec2(t.x,0.)).rgb,b=sampleAt(v_texCoord-vec2(t.x,0.)).rgb;
vec3 d=sampleAt(v_texCoord+vec2(0.,t.y)).rgb,e=sampleAt(v_texCoord-vec2(0.,t.y)).rgb;
vec3 mean=(a+b+d+e)*.25;
float ya=luma(a),yb=luma(b),yd=luma(d),ye=luma(e),yc=luma(c.rgb);
float hi=max(max(ya,yb),max(yd,ye)),lo=min(min(ya,yb),min(yd,ye));
float consistency=1.-smoothstep(threshold*.6,threshold*2.1,hi-lo);
float hot=smoothstep(threshold,threshold*1.5,yc-hi);
float cold=smoothstep(threshold,threshold*1.5,lo-yc)*repairDark;
float weight=max(hot,cold)*consistency*strength;
fragColor=vec4(mix(c.rgb,mean,weight),c.a);
}
