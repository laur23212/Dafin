#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float shrink;
uniform float feather;
uniform float radiusPx;

float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 texel(){return 1.0/max(u_resolution,vec2(1.0));}
vec2 uvClamp(vec2 uv){return clamp(uv,vec2(0.),vec2(1.));}
vec4 sampleAt(vec2 uv){return texture(u_currentTexture,uvClamp(uv));}
vec3 safeRGB(vec3 c){return clamp(c,vec3(0.),vec3(1.));}
float sat(float x){return clamp(x,0.,1.);}

void main(){

vec4 c=sampleAt(v_texCoord);if(u_hasClipMask<.5){fragColor=c;return;}
vec2 t=texel()*radiusPx;float m=driftMask(v_texCoord),mn=m;
mn=min(mn,driftMask(uvClamp(v_texCoord+vec2(t.x,0.))));
mn=min(mn,driftMask(uvClamp(v_texCoord-vec2(t.x,0.))));
mn=min(mn,driftMask(uvClamp(v_texCoord+vec2(0.,t.y))));
mn=min(mn,driftMask(uvClamp(v_texCoord-vec2(0.,t.y))));
float shape=mix(m,mn,shrink);float alpha=smoothstep(.5-feather,.5+feather,shape);
fragColor=vec4(c.rgb,c.a*alpha);
}
