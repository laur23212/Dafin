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
uniform float shadowNeutral;
uniform float highlightNeutral;
uniform float pivot;
void main(){vec4 src=texture(u_currentTexture,v_texCoord);
 float y=luminance(src.rgb);float bright=smoothstep(pivot-.15,pivot+.22,y);
 float warm=max(src.r-src.b,0.);float cool=max(src.b-src.r,0.);
 vec3 lower=src.rgb+vec3(cool*.28,cool*.07,-cool*.32)*shadowNeutral;
 vec3 upper=src.rgb+vec3(-warm*.30,-warm*.06,warm*.35)*highlightNeutral;
 vec3 mixed=mix(lower,upper,bright);
 fragColor=vec4(clamp(mix(src.rgb,mixed,strength),0.,1.),src.a);
}
