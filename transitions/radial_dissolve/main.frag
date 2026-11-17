#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window
uniform float softness;

void main() {
    vec4 a = texture(u_fromTexture, v_texCoord);
    vec4 b = texture(u_toTexture, v_texCoord);
    vec2 d = (v_texCoord - 0.5) * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0);
    float r = length(d) / 1.15;                      // 0 at the centre, below 1 in the corners
    float edge = u_progress * (1.0 + softness) - softness;
    float m = 1.0 - smoothstep(edge, edge + softness, r);
    fragColor = mix(a, b, m);
}
