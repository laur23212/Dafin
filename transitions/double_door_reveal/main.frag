#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
uniform float edgeShade;
uniform float seam;
uniform float finishAt;
vec4 straightMix(vec4 a,vec4 b,float t){
 float alpha=mix(a.a,b.a,t);
 vec3 mixed=mix(a.rgb*a.a,b.rgb*b.a,t);
 return vec4(alpha>0.000001?mixed/alpha:vec3(0.0),alpha);
}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 float p=clamp(u_progress,0.0,1.0);
 if(p<=0.0){fragColor=texture(u_fromTexture,uv);return;}
 if(p>=max(finishAt,0.72)){fragColor=texture(u_toTexture,uv);return;}
 float open=smoothstep(0.0,max(finishAt,0.72),p);
 float halfGap=0.5*open;
 float px=1.0/max(u_resolution.x,1.0);
 vec4 next=texture(u_toTexture,uv);
 float leftEdge=0.5-halfGap;
 float rightEdge=0.5+halfGap;
 float coverLeft=1.0-smoothstep(leftEdge-px,leftEdge+px,uv.x);
 float coverRight=smoothstep(rightEdge-px,rightEdge+px,uv.x);
 vec2 q=uv;
 q.x+=uv.x<0.5?halfGap:-halfGap;
 q=clamp(q,vec2(0.0),vec2(1.0));
 vec4 previous=texture(u_fromTexture,q);
 float side=uv.x<0.5?leftEdge:rightEdge;
 float distanceToEdge=abs(uv.x-side);
 float shadow=(1.0-smoothstep(0.0,max(seam,0.0001),distanceToEdge))*edgeShade;
 previous.rgb*=1.0-shadow;
 float cover=clamp(max(coverLeft,coverRight),0.0,1.0);
 fragColor=straightMix(next,previous,cover);
}
