#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float redMask;
uniform float blueMask;
uniform float exposure;
uniform float density;
void main(){
 vec4 s=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 // The orange film base normally over-represents red and under-represents blue.
 vec3 neg=1.0-clamp(s.rgb,0.0,1.0);
 neg.r*=1.0-redMask;neg.b*=1.0+blueMask;
 vec3 c=clamp((neg-.5)*(1.0+density)+.5+exposure,vec3(0.0),vec3(1.0));
 fragColor=vec4(c,s.a);
}