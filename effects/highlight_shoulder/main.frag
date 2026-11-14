#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float strength;
uniform float knee;
uniform float colorRetention;
void main(){
 vec4 s=texture(u_currentTexture,clamp(v_texCoord,vec2(0.0),vec2(1.0)));
 float y=dot(s.rgb,vec3(.2126,.7152,.0722));
 float t=max(y-knee,0.0)/max(1.0-knee,0.001);
 float y1=knee+(1.0-knee)*t/(1.0+strength*t*1.85);
 float target=mix(y,y1,smoothstep(knee,min(1.0,knee+.12),y));
 float scale=target/max(y,.001);
 vec3 keepHue=s.rgb*scale;
 vec3 keepLuma=s.rgb-(y-target)*vec3(1.0);
 vec3 outRGB=mix(keepLuma,keepHue,colorRetention);
 fragColor=vec4(clamp(outRGB,vec3(0.0),vec3(1.0)),s.a);
}
