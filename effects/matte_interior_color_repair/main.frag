#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float strength;
uniform float edgeWidthPx;
uniform float feather;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);if(u_hasClipMask<.5){fragColor=c;return;}
vec2 t=texel()*edgeWidthPx;float m=driftMask(v_texCoord);vec3 chosen=c.rgb;float best=m;
for(int i=0;i<8;i++){
 float a=6.2831853*float(i)/8.;vec2 uv=uvClamp(v_texCoord+t*vec2(cos(a),sin(a)));
 float qm=driftMask(uv);if(qm>best){best=qm;chosen=sampleAt(uv).rgb;}
}
float boundary=4.*m*(1.-m);float blend=strength*sat(boundary);
float alpha=smoothstep(.5-feather,.5+feather,m)*c.a;
fragColor=vec4(mix(c.rgb,chosen,blend),alpha);
}
