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
uniform float amount;
uniform float inkNoise;
uniform vec3 inkColor;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);
 if(u_hasClipMask<.5){fragColor=s;return;}
 float matte=clamp(driftMask(v_texCoord),0.,1.);
 vec2 px=v_texCoord*vec2(1080.*u_resolution.x/max(u_resolution.y,1.),1080.);float noise=(rnd(floor(px/20.))-.5)*inkNoise*.14;
 vec3 ink=clamp(inkColor+noise,0.,1.);
 fragColor=vec4(mix(s.rgb,ink,matte*amount),s.a);}
