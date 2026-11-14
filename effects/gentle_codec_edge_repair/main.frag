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
uniform float strength;
uniform float protect;
uniform float radius;

void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);vec2 px=radius/max(u_resolution,vec2(1.));
 vec3 sum=vec3(0.);float den=0.;float Y=lum(src.rgb);
 for(int i=-1;i<=1;i++)for(int j=-1;j<=1;j++){
  vec3 c=texture(u_currentTexture,clamp(uv+vec2(i,j)*px,0.,1.)).rgb;
  float dist=abs(lum(c)-Y);
  float w=exp(-dist*mix(15.,68.,protect));
  sum+=c*w;den+=w;
 }
 vec3 repaired=sum/max(den,.0001);
 float textureDifference=length(repaired-src.rgb);
 float weight=1.-smoothstep(.08,.28,textureDifference);
 fragColor=vec4(mix(src.rgb,repaired,strength*weight),src.a);}
