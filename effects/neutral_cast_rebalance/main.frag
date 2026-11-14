#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float strength;
uniform float preserveContrast;
void main(){
 vec4 src=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 vec3 avg=vec3(0.0);
 for(int y=0;y<3;y++)for(int x=0;x<3;x++){
  vec3 c=texture(u_currentTexture,(vec2(float(x),float(y))+.5)/3.0).rgb;
  avg+=c;
 }
 avg/=9.0;
 float grey=dot(avg,vec3(.299,.587,.114));
 vec3 gain=clamp(vec3(grey)/max(avg,vec3(.04)),vec3(.70),vec3(1.45));
 vec3 adjusted=src.rgb*gain;
 float before=dot(src.rgb,vec3(.2126,.7152,.0722));
 float after=dot(adjusted,vec3(.2126,.7152,.0722));
 adjusted=mix(adjusted,adjusted*before/max(after,.01),preserveContrast);
 fragColor=vec4(clamp(mix(src.rgb,adjusted,strength),vec3(0.0),vec3(1.0)),src.a);
}