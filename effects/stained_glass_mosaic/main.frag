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
uniform float cellSize;
uniform float leading;
uniform float colorBoost;

void main(){vec4 src=texture(u_currentTexture,v_texCoord);
 float cells=mix(34.,8.,cellSize);vec2 pos=v_texCoord*vec2(u_resolution.x/max(1.,u_resolution.y),1.)*cells;
 vec2 ip=floor(pos);float d1=1e5;float d2=1e5;vec2 best=pos;
 for(int j=-1;j<=1;j++)for(int i=-1;i<=1;i++){
 vec2 cell=ip+vec2(i,j);vec2 center=cell+vec2(.25+rnd(cell),.25+rnd(cell+17.1))*.5;
 float dst=length(center-pos);if(dst<d1){d2=d1;d1=dst;best=center;}else if(dst<d2){d2=dst;}}
 vec2 sampleUV=best/vec2(u_resolution.x/max(1.,u_resolution.y),1.)/cells;
 vec3 c=texture(u_currentTexture,clamp(sampleUV,0.,1.)).rgb;
 float Y=lum(c);c=mix(vec3(Y),c,1.+colorBoost*.75);
 float seam=1.-smoothstep(.014,.08,d2-d1);
 c=mix(clamp(c,0.,1.),vec3(.06,.075,.085),seam*leading);
 fragColor=vec4(c,src.a);}
