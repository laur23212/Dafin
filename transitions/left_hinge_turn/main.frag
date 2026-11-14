#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform float perspective;
uniform float panelShade;
uniform float finish;
vec4 overOp(vec4 a,vec4 b){float z=a.a+b.a*(1.0-a.a);vec3 v=a.rgb*a.a+b.rgb*b.a*(1.0-a.a);return vec4(z>1e-6?v/z:vec3(0.),z);}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 float p=clamp(u_progress,0.0,1.0);
 vec4 a=texture(u_fromTexture,uv),b=texture(u_toTexture,uv);
 if(p<=0.){fragColor=a;return;}
 if(p>=finish){fragColor=b;return;}
 float k=smoothstep(0.,max(finish,.5),p);
 // A single sheet collapses toward the LEFT hinge; no separate halves.
 float width=1.0-k;
 if(uv.x>width || width<=.001){fragColor=b;return;}
 float x=uv.x/max(width,.001);
 float perspectiveWarp=1.0+perspective*k*(1.0-x);
 float y=.5+(uv.y-.5)*perspectiveWarp;
 if(y<0.0 || y>1.0){fragColor=b;return;}
 vec4 sheet=texture(u_fromTexture,clamp(vec2(x,y),vec2(0.0),vec2(1.0)));
 sheet.rgb*=1.0-panelShade*k*(.35+.65*x);
 fragColor=overOp(sheet,b);
}
