#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_fromTexture;
uniform sampler2D u_toTexture;
uniform float u_progress;
uniform vec2 u_resolution;
uniform vec2 center;
uniform float softness;

// Dafin: the circle grows to the corner farthest from `center`, so the incoming clip covers the
// whole frame exactly at the end wherever the centre is (the delivered version stopped at a fixed
// radius and left part of the old clip until the last frame). The feather never reaches zero
// width, since smoothstep with equal edges is undefined.
void main(){
    if (u_progress <= 0.0) { fragColor = texture(u_fromTexture, v_texCoord); return; }
    if (u_progress >= 1.0) { fragColor = texture(u_toTexture, v_texCoord); return; }
    vec2 aspect = vec2(u_resolution.x / max(1.0, u_resolution.y), 1.0);
    float r = length((v_texCoord - center) * aspect);
    float feather = max(softness, 1e-4);
    float reach = length(max(center, 1.0 - center) * aspect) + 2.0 * feather;
    float radius = u_progress * reach;
    float mask = 1.0 - smoothstep(radius - feather, radius + feather, r);
    fragColor = mix(texture(u_fromTexture, v_texCoord), texture(u_toTexture, v_texCoord), mask);
}
