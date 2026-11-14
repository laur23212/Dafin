#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform float density;
uniform float inkStrength;
uniform float lineWidth;
uniform vec3 inkColor;
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 vec4 src=texture(u_currentTexture,uv);
 if(u_hasDepth<.5){fragColor=src;return;}
 float d=clamp(driftDepthGuided(uv,u_currentTexture),0.0,1.0);
 float phase=fract(d*density);
 float fw=max(fwidth(d*density),.006);
 float dist=min(phase,1.0-phase);
 float line=1.0-smoothstep(lineWidth*fw, (lineWidth+1.3)*fw,dist);
 vec3 outRGB=mix(src.rgb,inkColor,clamp(line*inkStrength,0.0,1.0));
 fragColor=vec4(outRGB,src.a);
}
