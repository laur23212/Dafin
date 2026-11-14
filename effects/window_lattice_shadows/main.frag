#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform int u_frameIndex;
uniform float u_frameRate;
float luminance(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
vec2 mirrored(vec2 v){return 1.0-abs(mod(v,2.0)-1.0);}
float hash21(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float secondsNow(){return float(u_frameIndex)/max(u_frameRate,1.0);}
uniform float strength;
uniform float bars;
uniform float angle;
uniform float softness;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 float a=(angle-.5)*1.2;vec2 p=uv-vec2(.5);mat2 rot=mat2(cos(a),-sin(a),sin(a),cos(a));
 p=rot*p;p.x*=u_resolution.x/max(1.,u_resolution.y);
 float gx=abs(fract(p.x*mix(2.5,7.,bars))-.5);
 float gy=abs(fract(p.y*mix(2.,5.,bars))-.5);
 float thick=mix(.08,.19,bars);float feather=mix(.005,.04,softness);
 float mullion=1.-smoothstep(thick-feather,thick+feather,min(gx,gy));
 float light=1.-strength*.65*mullion;
 fragColor=vec4(src.rgb*light,src.a);
}
