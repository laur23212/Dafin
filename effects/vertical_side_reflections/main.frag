#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float centerWidth;
uniform float sideDim;
uniform float separator;
uniform vec3 separatorColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 float w=clamp(centerWidth,.2,.8);
 float l=.5-w*.5;float r=.5+w*.5;
 // NEVER squeeze the source: the central panel is a true uncropped-pixel-aspect crop.
 bool inside=(uv.x>=l && uv.x<=r);
 // Side samples mirror the adjacent picture at the panel boundary.
 float sx=uv.x<l? l+(l-uv.x) : r-(uv.x-r);
 vec2 q=vec2(inside?uv.x:sx,uv.y);
 vec4 result=texture(u_currentTexture,clamp(q,vec2(0.0),vec2(1.0)));
 if(!inside)result.rgb*=1.0-sideDim;
 if(abs(uv.x-l)<separator || abs(uv.x-r)<separator){
   result.rgb=mix(result.rgb,separatorColor,.76);
 }
 fragColor=result;
}
