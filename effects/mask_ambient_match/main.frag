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


uniform float blend,radius,retainContrast;
void main(){vec2 uv=v_texCoord;vec4 c=src(uv);if(u_hasClipMask<.5){fragColor=c;return;}float m=driftMask(uv);vec2 d=px()*mix(16.,70.,radius);vec3 s=vec3(0.);float w=0.;for(int i=0;i<8;i++){float a=float(i)*.785398163;vec2 p=clamp(uv+vec2(cos(a),sin(a))*d,vec2(0.0),vec2(1.0));float k=1.-driftMask(p);s+=src(p).rgb*k;w+=k;}vec3 amb=s/max(w,.001);float y=lum(c.rgb);vec3 shifted=c.rgb+(amb-c.rgb)*blend*.27;shifted=mix(vec3(lum(shifted)),shifted,retainContrast);fragColor=vec4(sat3(mix(c.rgb,shifted,m*sat(w))),c.a);}

