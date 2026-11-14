#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float cropZoom;
uniform float gap;
uniform vec3 dividerColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 vec2 id=step(vec2(.5),uv);
 vec2 local=fract(uv*2.0);
 vec2 centers=vec2(mix(.36,.64,id.x),mix(.37,.63,id.y));
 vec2 q=clamp(centers+(local-.5)/max(cropZoom,1.0),vec2(0.0),vec2(1.0));
 vec4 src=texture(u_currentTexture,q);
 float line=(1.0-step(gap,abs(uv.x-.5)))*(1.0) + (1.0-step(gap,abs(uv.y-.5)));
 src.rgb=mix(src.rgb,dividerColor,clamp(line,0.0,1.0));
 fragColor=src;
}