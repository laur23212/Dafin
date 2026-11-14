#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float sat(float x){return clamp(x,0.0,1.0);}
vec3 sat3(vec3 x){return clamp(x,vec3(0.0),vec3(1.0));}
float lum(vec3 c){return dot(c,vec3(0.2126,0.7152,0.0722));}
vec2 px(){return 1.0/max(u_resolution,vec2(1.0));}
vec4 src(vec2 p){return texture(u_currentTexture,clamp(p,vec2(0.0001),vec2(0.9999)));}
float H(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}


uniform float routing,mixAmount,lumaCarry;
void main(){vec4 c=src(v_texCoord);int mode=int(floor(routing+.5));vec3 o=c.rgb;if(mode==0)o=c.rgb;if(mode==1)o=c.gbr;if(mode==2)o=c.brg;if(mode==3)o=c.rbg;if(mode==4)o=c.grb;if(mode==5)o=c.bgr;float y=lum(c.rgb),z=lum(o);o=mix(o,o*clamp(y/(z+.001),.3,3.0),lumaCarry);fragColor=vec4(sat3(mix(c.rgb,o,mixAmount)),c.a);}

