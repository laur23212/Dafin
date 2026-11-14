#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float gutter;
uniform float crop;
uniform float offset;
uniform vec3 gutterColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 vec4 original=texture(u_currentTexture,uv);
 float gap=clamp(gutter,0.0,0.045);
 float panel=(1.0-4.0*gap)/3.0;
 float x=uv.x;
 float idx=-1.0;
 float local=0.5;
 for(int i=0;i<3;i++){
   float start=gap+float(i)*(panel+gap);
   if(x>=start&&x<=start+panel){ idx=float(i);local=(x-start)/max(panel,0.01); }
 }
 if(idx<0.0){fragColor=vec4(gutterColor,original.a);return;}
 float center=0.5+(idx-1.0)*offset;
 vec2 sampleUV=vec2(center+(local-0.5)*crop,uv.y);
 fragColor=texture(u_currentTexture,clamp(sampleUV,vec2(0.0),vec2(1.0)));
}
