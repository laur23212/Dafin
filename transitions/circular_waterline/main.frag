#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
vec2 mirrorUV(vec2 v){vec2 m=mod(v,2.0);return 1.0-abs(m-1.0);}
float rnd(vec2 x){return fract(sin(dot(x,vec2(127.1,311.7)))*43758.5453123);}
vec4 blendStraight(vec4 a,vec4 b,float t){
 t=clamp(t,0.,1.); float alpha=mix(a.a,b.a,t);
 return vec4((a.rgb*a.a*(1.-t)+b.rgb*b.a*t)/max(alpha,0.00001),alpha);
}
uniform float refraction;
uniform float softness;
void main(){
 vec2 uv=v_texCoord;vec4 a=texture(u_fromTexture,uv);vec4 b=texture(u_toTexture,uv);
 if(u_progress<=0.0){fragColor=a;return;}
 if(u_progress>=0.985){fragColor=b;return;}
 float p=clamp(u_progress/0.985,0.,1.);
vec2 d=uv-.5;d.x*=u_resolution.x/max(1.,u_resolution.y);
 float dist=length(d);float radius=p*1.25;
 float wave=exp(-abs(dist-radius)*35.)*sin((dist-radius)*88.)*refraction*.016;
 vec2 dir=normalize(d+vec2(.0001));vec2 displaced=mirrorUV(uv+dir*wave);
 vec4 sa=texture(u_fromTexture,displaced);vec4 sb=texture(u_toTexture,displaced);
 float m=1.-smoothstep(radius-softness*.025-.004,radius+softness*.025+.004,dist);
 fragColor=blendStraight(sa,sb,m);
}
