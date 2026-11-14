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
uniform float density;
uniform float ink;
uniform float paper;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);float Y=lum(s.rgb);
 vec2 px=v_texCoord*vec2(1080.*u_resolution.x/max(u_resolution.y,1.),1080.);float pitch=mix(46.,16.,density);
 float l1=abs(fract((px.x+px.y*.72)/pitch)-.5);
 float l2=abs(fract((px.x-px.y*.68)/pitch)-.5);
 float d1=1.-smoothstep(.065,.16,l1);float d2=1.-smoothstep(.05,.15,l2);
 float stroke=max(d1*(1.-smoothstep(.2,.7,Y)),d2*(1.-smoothstep(.04,.43,Y)));
 vec3 base=mix(vec3(.97,.96,.90),vec3(.91,.86,.73),paper);
 base=mix(base,vec3(.11,.13,.16),stroke*ink);
 base*=mix(.72,1.,Y);
 fragColor=vec4(base,s.a);}
