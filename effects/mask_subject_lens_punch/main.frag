#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 focus;
uniform float zoom;
uniform float blend;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));vec4 s=texture(u_currentTexture,uv);
 if(u_hasClipMask<.5){fragColor=s;return;}
 float m=driftMask(uv);
 vec2 q=clamp(focus+(uv-focus)/max(zoom,1.0),vec2(0.0),vec2(1.0));
 vec4 mag=texture(u_currentTexture,q);
 vec3 rgb=mix(s.rgb,mag.rgb,clamp(blend*m,0.0,1.0));
 fragColor=vec4(rgb,s.a);
}