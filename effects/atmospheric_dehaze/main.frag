#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float strength;
uniform float radius;
uniform float highlightGuard;
vec4 src(vec2 q){return texture(u_currentTexture,clamp(q,vec2(0.0),vec2(1.0)));}
float dark(vec3 c){return min(c.r,min(c.g,c.b));}
void main(){
 vec2 px=1.0/max(u_resolution,vec2(1.0));
 vec4 c=src(v_texCoord);
 float local=dark(c.rgb);
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
   vec2 o=vec2(float(x),float(y))*radius*px;
   local=min(local,dark(src(v_texCoord+o).rgb));
 }
 // A fixed neutral airlight is an artistic assumption, not a measured atmospheric model.
 vec3 air=vec3(0.88,0.91,0.94);
 float transmission=clamp(1.0-strength*local/0.9,0.48,1.0);
 vec3 restored=(c.rgb-air*(1.0-transmission))/max(transmission,0.48);
 float bright=max(c.r,max(c.g,c.b));
 float guard=1.0-highlightGuard*smoothstep(0.74,0.98,bright);
 vec3 result=mix(c.rgb,clamp(restored,vec3(0.0),vec3(1.0)),guard);
 fragColor=vec4(clamp(result,vec3(0.0),vec3(1.0)),c.a);
}
