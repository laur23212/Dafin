#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
vec2 mirrorUV(vec2 v){vec2 t=mod(v,2.0);return 1.0-abs(t-1.0);}
float luma(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
float rand2(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453123);}
float tm(){return float(u_frameIndex)/max(1.0,u_frameRate);}
uniform float weaveStrength;
uniform float threadScale;
uniform float warmth;
void main(){vec4 src=texture(u_currentTexture,v_texCoord);vec2 pix=v_texCoord*u_resolution;
 float period=mix(6.,2.5,threadScale);
 float wx=sin(pix.x*6.2831853/period),wy=sin(pix.y*6.2831853/period);
 float cross=.5+.5*wx*wy;float thread=.55+.45*max(abs(wx),abs(wy));
 float modv=(cross-.5)*.12+(thread-.75)*.10;
 vec3 cloth=src.rgb+modv*weaveStrength+vec3(warmth*.036,warmth*.019,-warmth*.016)*weaveStrength;
 fragColor=vec4(clamp(cloth,0.,1.),src.a);}
