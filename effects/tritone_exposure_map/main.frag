#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float luminance(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
vec2 mirrored(vec2 v){return 1.0-abs(mod(v,2.0)-1.0);}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float secondsNow(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;
uniform float midpoint;
uniform float blendWidth;
uniform vec3 shadowInk;
uniform vec3 midInk;
uniform vec3 lightInk;
void main(){vec4 src=texture(u_currentTexture,v_texCoord);
 float y=luminance(src.rgb);
 float lo=1.-smoothstep(midpoint-blendWidth,midpoint+blendWidth,y);
 float hi=smoothstep(midpoint+blendWidth*.4,midpoint+blendWidth*1.8,y);
 vec3 ink=mix(shadowInk,midInk,1.-lo);ink=mix(ink,lightInk,hi);
 fragColor=vec4(mix(src.rgb,ink,strength),src.a);
}
