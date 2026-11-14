#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float curlWidth;
uniform float tilt;
uniform float shadow;
uniform float underside;
vec4 pmMix(vec4 a,vec4 b,float t){
    float alpha=mix(a.a,b.a,t);
    vec3 premul=mix(a.rgb*a.a,b.rgb*b.a,t);
    return vec4(alpha>1e-6?premul/alpha:vec3(0.0),alpha);
}
void main(){
    float p=clamp(u_progress,0.0,1.0);
    vec2 uv=v_texCoord;
    if(p<=0.0){fragColor=texture(u_fromTexture,uv);return;}
    if(p>=1.0){fragColor=texture(u_toTexture,uv);return;}
    // A normalized diagonal coordinate: incoming side is behind the moving fold.
    vec2 w=vec2(.6+.34*tilt,.4-.34*tilt);
    float position=dot(uv,w);
    float sweep=smoothstep(0.0,1.0,p);
    float edge=mix(-.24,1.26,sweep);
    float dist=position-edge;
    float antialias=max(1.4/min(u_resolution.x,u_resolution.y),.001);
    vec4 old=texture(u_fromTexture,uv);
    vec4 incoming=texture(u_toTexture,uv);
    if(dist<0.0){
        float nearFold=1.0-smoothstep(-.065,0.0,dist);
        float shade=shadow*(1.0-nearFold);
        incoming.rgb*=1.0-shade*.56;
        fragColor=incoming;
        return;
    }
    if(dist>curlWidth){fragColor=old;return;}
    // Mirror the folded portion around the moving fold line, darken as if looking
    // at the underside of a sheet, then lift its rim with a controlled highlight.
    vec2 reflected=clamp(uv-2.0*dist*w/max(dot(w,w),.001),0.0,1.0);
    vec4 undersideColor=texture(u_fromTexture,reflected);
    float t=clamp(dist/max(curlWidth,.001),0.0,1.0);
    float roll=sin(t*3.14159265359);
    float shading=1.0-underside*(.25+.7*roll);
    undersideColor.rgb=clamp(undersideColor.rgb*shading+vec3(.09*roll),0.0,1.0);
    // Blend to the original face at the far end of the curl to avoid a hard ridge.
    undersideColor=pmMix(undersideColor,old,smoothstep(.76,1.0,t));
    float revealEdge=smoothstep(0.0,antialias,dist);
    fragColor=pmMix(incoming,undersideColor,revealEdge);
}

