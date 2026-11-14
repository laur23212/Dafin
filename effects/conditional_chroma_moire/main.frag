#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float strength;
uniform float sensitivity;
uniform float radiusPx;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);vec2 t=texel()*radiusPx;float y=luma(c.rgb);
vec3 p1=sampleAt(v_texCoord+vec2(t.x,0.)).rgb,p2=sampleAt(v_texCoord-vec2(t.x,0.)).rgb;
vec3 p3=sampleAt(v_texCoord+vec2(0.,t.y)).rgb,p4=sampleAt(v_texCoord-vec2(0.,t.y)).rgb;
vec3 ave=(p1+p2+p3+p4)*.25;float am=luma(ave);
vec2 chroma=vec2(c.r-y,c.b-y),chromaAvg=vec2(ave.r-am,ave.b-am);
float chromaError=length(chroma-chromaAvg);
float yDetail=(abs(y-luma(p1))+abs(y-luma(p2))+abs(y-luma(p3))+abs(y-luma(p4)))*.25;
float activation=smoothstep(sensitivity*.5,sensitivity*1.6,chromaError)* (1.-smoothstep(sensitivity*.2,sensitivity*2.,yDetail));
vec2 z=mix(chroma,chromaAvg,activation*strength);
vec3 corrected=vec3(y+z.x,y-(.2126*z.x+.0722*z.y)/.7152,y+z.y);
fragColor=vec4(safeRGB(corrected),c.a);
}
