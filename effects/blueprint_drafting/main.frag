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
uniform float lineStrength;
uniform float detailThreshold;
uniform float gridAmount;
uniform float paperTint;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 vec2 px=1.0/max(u_resolution,vec2(1.0));
 float leftY=luminance(texture(u_currentTexture,clamp(uv-vec2(px.x,0.),0.,1.)).rgb);
 float rightY=luminance(texture(u_currentTexture,clamp(uv+vec2(px.x,0.),0.,1.)).rgb);
 float topY=luminance(texture(u_currentTexture,clamp(uv-vec2(0.,px.y),0.,1.)).rgb);
 float botY=luminance(texture(u_currentTexture,clamp(uv+vec2(0.,px.y),0.,1.)).rgb);
 float edge=length(vec2(rightY-leftY,botY-topY))*4.0;
 float drawing=smoothstep(detailThreshold,detailThreshold+.23,edge)*lineStrength;
 vec2 m=abs(fract(uv*vec2(20.,12.))-.5);
 float grid=smoothstep(.478,.5,max(m.x,m.y));
 vec3 base=mix(vec3(.015,.065,.125),vec3(.03,.125,.20),paperTint);
 vec3 outc=base+vec3(.018,.24,.31)*grid*gridAmount;
 outc=mix(outc,vec3(.73,.93,1.),drawing);
 fragColor=vec4(clamp(outc,0.,1.),src.a);
}
