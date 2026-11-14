#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform float cardScale;
uniform float shadow;
uniform float cornerSoftness;
vec4 overOp(vec4 a,vec4 b){float z=a.a+b.a*(1.0-a.a);vec3 v=a.rgb*a.a+b.rgb*b.a*(1.0-a.a);return vec4(z>1e-6?v/z:vec3(0.),z);}
void main(){
 vec2 uv=clamp(v_texCoord,vec2(0.0),vec2(1.0));
 float p=clamp(u_progress,0.0,1.0);
 vec4 a=texture(u_fromTexture,uv),b=texture(u_toTexture,uv);
 if(p<=0.0){fragColor=a;return;}
 if(p>=.92){fragColor=b;return;}
 float t=smoothstep(0.,.92,p);
 float sc=mix(1.0,cardScale,t);
 vec2 q=(uv-.5)/max(sc,.05)+.5;
 float bounds=max(abs(q.x-.5),abs(q.y-.5));
 float coverage=1.0-smoothstep(.49-cornerSoftness,.50,bounds);
 // Recede and tilt subtly as the next scene fills the canvas.
 q.x+=(q.y-.5)*t*.09;
 bool valid=all(greaterThanEqual(q,vec2(0.0))) && all(lessThanEqual(q,vec2(1.0)));
 float fade=1.0-smoothstep(.65,.92,p);
 vec4 card=valid?texture(u_fromTexture,clamp(q,vec2(0.),vec2(1.))):vec4(0.);
 card.a*=coverage*fade;
 // A subtle cast shadow follows the scaled card and never covers the full scene.
 vec2 sq=(uv-vec2(.012,.018)-.5)/max(sc,.05)+.5;
 float shape=max(abs(sq.x-.5),abs(sq.y-.5));
 float sh=(1.0-smoothstep(.45,.58,shape))*shadow*fade;
 b.rgb*=1.0-sh*.52;
 fragColor=overOp(card,b);
}
