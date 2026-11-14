#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform int u_frameIndex;
uniform float u_frameRate;
uniform float outerBlur;
uniform float radius;
uniform float feather;
uniform vec2 focus;

void main(){

 vec4 s=texture(u_currentTexture,v_texCoord);vec2 p=3.0/max(vec2(1.),u_resolution);vec3 avg=vec3(0.);
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)avg+=texture(u_currentTexture,clamp(v_texCoord+vec2(x,y)*p,0.,1.)).rgb;
 avg/=9.;float d=length((v_texCoord-focus)*vec2(u_resolution.x/max(1.,u_resolution.y),1.));float b=smoothstep(radius,radius+feather,d)*outerBlur;
 fragColor=vec4(mix(s.rgb,avg,b),s.a);
 
}
