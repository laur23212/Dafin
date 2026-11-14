#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float divide;
uniform float rightExposure;
uniform float rightContrast;
uniform float lineWidth;
uniform vec3 lineColor;
void main(){
 vec2 uv=v_texCoord;vec4 s=texture(u_currentTexture,clamp(uv,vec2(0.0),vec2(1.0)));
 if(uv.x>=divide){
   vec3 tuned=clamp((s.rgb-.5)*(1.0+rightContrast)+.5+rightExposure,vec3(0.0),vec3(1.0));
   s.rgb=tuned;
 }
 if(abs(uv.x-divide)<=lineWidth*.5){s.rgb=mix(s.rgb,lineColor,.9);}
 fragColor=s;
}
