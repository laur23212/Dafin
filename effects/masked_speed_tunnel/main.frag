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
uniform float speed;
uniform float reach;
uniform float softEdge;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 if(u_hasClipMask<.5){fragColor=src;return;}
 float mask=clamp(driftMask(uv),0.,1.);
 float safe=smoothstep(.25-.2*softEdge,.83+.13*softEdge,mask);
 vec2 delta=uv-vec2(.5);vec3 blur=vec3(0.);float denom=0.;
 for(int i=0;i<8;i++){
 float q=float(i)/7.;vec2 sampleUV=clamp(uv-delta*q*reach*.18,0.,1.);
 float w=1.0-q*.63;blur+=texture(u_currentTexture,sampleUV).rgb*w;denom+=w;
 }
 vec3 bg=mix(src.rgb,blur/max(denom,.01),speed);
 fragColor=vec4(mix(bg,src.rgb,safe),src.a);
}
