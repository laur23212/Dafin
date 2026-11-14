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
uniform float pitch;
uniform float gap;
uniform float brightness;
uniform float colorMix;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 float cells=mix(65.,175.,pitch);
 vec2 grid=uv*vec2(cells*u_resolution.x/max(u_resolution.y,1.),cells);
 vec2 id=floor(grid);vec2 sub=fract(grid);
 vec3 sampleC=texture(u_currentTexture,clamp((id+.5)/vec2(cells*u_resolution.x/max(u_resolution.y,1.),cells),0.,1.)).rgb;
 float lane=floor(sub.x*3.0);float xlocal=fract(sub.x*3.0);
 float dotSize=mix(.15,.34,gap);
 float diode=1.-smoothstep(dotSize,.5,length(vec2(xlocal-.5,sub.y-.5)));
 float channel=(lane<.5?sampleC.r:(lane<1.5?sampleC.g:sampleC.b));
 vec3 on=lane<.5?vec3(1.,.12,.09):(lane<1.5?vec3(.13,1.,.14):vec3(.15,.30,1.));
 vec3 outc=on*channel*diode*brightness*2.1+vec3(.007);
 fragColor=vec4(clamp(mix(src.rgb,outc,colorMix),0.,1.),src.a);
}
