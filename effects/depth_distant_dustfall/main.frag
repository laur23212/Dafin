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
uniform float size;
uniform float depthStart;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);
 if(u_hasDepth<.5){fragColor=s;return;}
 float dp=driftDepthGuided(v_texCoord,u_currentTexture);
 vec2 pos=v_texCoord*vec2(28.,17.);
 float t=float(u_frameIndex)/max(u_frameRate,1.);
 pos.y+=t*.25;vec2 id=floor(pos);
 vec2 j=vec2(rnd(id),rnd(id+31.));
 vec2 f=fract(pos)-j;
 float d=length(f*vec2(1.,1.2));
 float mote=(1.-smoothstep(.015,.23+size*.11,d))*step(.73,rnd(id+67.));
 float behind=1.-smoothstep(depthStart-.18,depthStart+.12,dp);
 vec3 glow=vec3(.95,.91,.80)*mote*behind*amount;
 fragColor=vec4(clamp(s.rgb+glow,0.,1.),s.a);}
