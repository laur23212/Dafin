#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float skew;
uniform float pivot;
uniform float subtleCurve;
vec2 mirrored(vec2 p){return 1.0-abs(mod(p,vec2(2.0))-1.0);}
void main(){
 vec2 uv=v_texCoord;
 float t=uv.y-pivot;
 float delta=skew*t*(1.0+subtleCurve*abs(t)*2.0);
 vec2 q=mirrored(vec2(uv.x+delta,uv.y));
 fragColor=texture(u_currentTexture,clamp(q,vec2(0.0),vec2(1.0)));
}
