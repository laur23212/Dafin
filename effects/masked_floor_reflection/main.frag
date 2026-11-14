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
uniform float horizon;
uniform float falloff;
uniform float ripple;
void main(){vec2 uv=v_texCoord;vec4 src=texture(u_currentTexture,uv);
 if(u_hasClipMask<.5){fragColor=src;return;}
 if(uv.y<=horizon){fragColor=src;return;}
 float dist=uv.y-horizon;
 vec2 reflected=vec2(uv.x+sin(uv.y*88.)*dist*ripple*.023,horizon-dist);
 if(reflected.y<0.0){fragColor=src;return;}
 reflected=clamp(reflected,0.,1.);
 float m=clamp(driftMask(reflected),0.,1.);
 float fade=clamp(1.-dist/max(.02,1.-horizon),0.,1.);
 fade=mix(fade*fade*fade,fade,falloff)*strength*m;
 vec4 refl=texture(u_currentTexture,reflected);
 fragColor=vec4(mix(src.rgb,refl.rgb,fade),src.a);
}
