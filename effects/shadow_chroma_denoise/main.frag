#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float amount;
uniform float shadowGate;
uniform float edgeProtect;
float luma(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));vec4 s=texture(u_currentTexture,uv);
 float y=luma(s.rgb);vec3 sum=vec3(0.0);float wsum=0.0;
 vec2 px=1.0/max(u_resolution,vec2(1.0));
 for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
  vec3 q=texture(u_currentTexture,clamp(uv+vec2(float(dx),float(dy))*px*1.5,vec2(0.0),vec2(1.0))).rgb;
  float dif=abs(luma(q)-y);
  float ws=exp(-.28*float(dx*dx+dy*dy))*exp(-dif/max(edgeProtect,.005));
  sum+=q*ws;wsum+=ws;
 }
 vec3 smoothCol=sum/max(wsum,1e-5);
 vec3 balanced=smoothCol+vec3(y-luma(smoothCol));
 float dark=1.0-smoothstep(shadowGate,shadowGate+.32,y);
 vec3 rgb=mix(s.rgb,balanced,clamp(amount*dark,0.0,1.0));
 fragColor=vec4(clamp(rgb,vec3(0.0),vec3(1.0)),s.a);
}