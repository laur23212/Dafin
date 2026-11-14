#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float splitAt;
uniform float detailZoom;
uniform vec2 detailFocus;
uniform float separator;
uniform vec3 lineColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 bool right=uv.x>=splitAt;
 vec2 rightCenter=vec2(splitAt+(1.0-splitAt)*.5,.5);
 vec2 leftCenter=vec2(splitAt*.5,.5);
 vec2 q=right?detailFocus+(uv-rightCenter)/max(detailZoom,1.0):vec2(.5)+(uv-leftCenter);
 vec4 src=texture(u_currentTexture,clamp(q,vec2(0.0),vec2(1.0)));
 float line=1.0-smoothstep(separator*.3,separator,abs(uv.x-splitAt));
 src.rgb=mix(src.rgb,lineColor,line);
 fragColor=src;
}