#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

void main() {
    float p = u_progress;
    vec2 c = vec2(0.5);
    vec2 uvA = c + (v_texCoord - c) / (1.0 + 0.15 * p);          // outgoing drifts in
    vec2 uvB = c + (v_texCoord - c) / (1.15 - 0.15 * p);         // incoming settles
    fragColor = mix(texture(u_fromTexture, uvA), texture(u_toTexture, uvB), p * p * (3.0 - 2.0 * p));
}
