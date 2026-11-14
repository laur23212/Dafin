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
uniform float brightness;
uniform float scale;
uniform float speed;
uniform float coolness;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 float t=secondsNow()*speed;
 vec2 p=uv*mix(9.,28.,scale);
 float a=sin(p.x+2.5*sin(p.y*.77+t*.5)+t*1.3);
 float b=sin(p.y*1.27+2.2*cos(p.x*.82-t*.42)-t*.95);
 float c=sin((p.x+p.y)*.59+sin(p.x*.5+p.y*.7+t*.8)*3.1);
 float caustic=1.-smoothstep(.11,.43,abs(a+b+c)*.333);
 caustic=caustic*caustic;
 vec3 tint=mix(vec3(1.,.95,.77),vec3(.68,.89,1.),coolness);
 fragColor=vec4(clamp(src.rgb+brightness*.44*caustic*tint,0.,1.),src.a);
}
