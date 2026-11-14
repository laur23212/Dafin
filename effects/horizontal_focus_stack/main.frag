#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float topWeight;
uniform float lowerZoom;
uniform float seamSize;
uniform vec3 seamColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 bool upper=uv.y<topWeight;
 vec2 panelCenter=upper?vec2(.5,topWeight*.5):vec2(.5,topWeight+(1.0-topWeight)*.5);
 vec2 q=upper?vec2(.5)+(uv-panelCenter):vec2(.5)+(uv-panelCenter)/max(lowerZoom,1.0);
 q=clamp(q,vec2(0.0),vec2(1.0));
 vec4 s=texture(u_currentTexture,q);
 float seam=1.0-smoothstep(seamSize*.2,seamSize,abs(uv.y-topWeight));
 s.rgb=mix(s.rgb,seamColor,seam);
 fragColor=s;
}