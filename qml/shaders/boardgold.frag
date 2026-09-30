#version 440
// The header of the menu board in any language (BoardText.qml): white text in, the plank's gold letters out -
// a bevelled face lit from the upper left, the letter's own depth going down-right, a dark rim and a soft
// shadow on the plank. «base» is the gold of the original letters in this picture (it carries the light of
// the time of day).
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 base;        // the gold of this picture
    vec2 texel;       // 1 / item size
};
layout(binding = 1) uniform sampler2D src;

float A(vec2 uv) { return texture(src, uv).a; }

void main()
{
    vec2 uv = qt_TexCoord0;
    float face = A(uv);

    // bevel: the slope of the (slightly blurred) letter edge is the normal of the face
    float r = 2.2;
    float ax = (A(uv + vec2(texel.x * r, 0.0)) - A(uv - vec2(texel.x * r, 0.0)))
             + 0.5 * (A(uv + vec2(texel.x * r * 2.0, 0.0)) - A(uv - vec2(texel.x * r * 2.0, 0.0)));
    float ay = (A(uv + vec2(0.0, texel.y * r)) - A(uv - vec2(0.0, texel.y * r)))
             + 0.5 * (A(uv + vec2(0.0, texel.y * r * 2.0)) - A(uv - vec2(0.0, texel.y * r * 2.0)));
    vec3 n = normalize(vec3(-ax, -ay, 0.55));
    vec3 L = normalize(vec3(-0.45, -0.75, 0.75));
    float diff = clamp(dot(n, L), 0.0, 1.0);
    float spec = pow(clamp(dot(reflect(-L, n), vec3(0.0, 0.0, 1.0)), 0.0, 1.0), 14.0);
    vec3 gold = base.rgb * (0.50 + 0.78 * diff) * mix(1.14, 0.84, uv.y) + vec3(1.0, 0.95, 0.80) * spec * 0.42;

    // the depth of the letter: its side, seen below and to the right
    float side = 0.0;
    for (int k = 1; k <= 5; ++k)
        side = max(side, A(uv - vec2(0.55, 1.0) * texel * float(k)));
    vec3 sideCol = base.rgb * 0.42;

    // a thin dark rim around the letter and its side
    float rim = 0.0;
    for (int k = 0; k < 8; ++k) {
        float a = 6.2831853 * float(k) / 8.0;
        vec2 d = vec2(cos(a), sin(a)) * 1.6;
        rim = max(rim, A(uv + d * texel));
        rim = max(rim, A(uv - vec2(0.55, 1.0) * texel * 5.0 + d * texel));
    }
    vec3 rimCol = base.rgb * 0.20;

    // a soft shadow on the plank, down and to the right
    float sh = 0.0;
    for (int k = 0; k < 9; ++k) {
        vec2 o = vec2(float(k % 3) - 1.0, float(k / 3) - 1.0) * 2.5;
        sh += A(uv - (vec2(4.0, 7.0) + o) * texel);
    }
    sh = sh / 9.0 * 0.55;

    // premultiplied, back to front: shadow, rim, side, face
    vec4 col = vec4(0.0, 0.0, 0.0, sh);
    col = vec4(rimCol * rim, rim) + col * (1.0 - rim);
    col = vec4(sideCol * side, side) + col * (1.0 - side);
    col = vec4(gold * face, face) + col * (1.0 - face);
    fragColor = col * qt_Opacity;
}
