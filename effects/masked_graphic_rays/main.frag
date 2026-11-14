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
uniform float bars;
uniform float movement;
uniform vec3 rayColor;

void main(){vec4 s=texture(u_currentTexture,v_texCoord);
 if(u_hasClipMask<.5){fragColor=s;return;}
 float m=clamp(driftMask(v_texCoord),0.,1.);
 vec2 p=v_texCoord-vec2(.5,.52);p.x*=u_resolution.x/max(u_resolution.y,1.);
 float a=atan(p.y,p.x)+movement*float(u_frameIndex)/max(u_frameRate,1.)*.17;
 float rays=step(.5,fract(a*mix(6.,19.,bars)/6.2831853));
 float backdrop=1.-m;
 vec3 outc=mix(s.rgb,mix(s.rgb,rayColor, .48+.38*rays),strength*backdrop);
 fragColor=vec4(outc,s.a);}
