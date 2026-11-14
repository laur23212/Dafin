#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float amount;
uniform float pivot;
uniform float span;
void main(){
 vec4 c=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 float y=dot(c.rgb,vec3(0.2126,0.7152,0.0722));
 float lo=1.0-smoothstep(pivot-span,pivot-span*0.28,y);
 float hi=smoothstep(pivot+span*0.28,pivot+span,y);
 float select=clamp(1.0-max(lo,hi),0.0,1.0);
 vec3 invert=vec3(1.0)-c.rgb;
 fragColor=vec4(clamp(mix(c.rgb,invert,amount*select),vec3(0.0),vec3(1.0)),c.a);
}
