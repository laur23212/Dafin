#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

void main() {
    vec4 a = texture(u_fromTexture, v_texCoord);
    vec4 b = texture(u_toTexture, v_texCoord);
    float flash = pow(sin(3.14159265 * u_progress), 3.0);    // white flash peaks at the middle
    vec4 c = mix(a, b, smoothstep(0.35, 0.65, u_progress));
    fragColor = vec4(mix(c.rgb, vec3(1.0), flash * 0.85), c.a);
}
