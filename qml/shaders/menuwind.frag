#version 440
// The living menu (MainMenuScreen / LiveBoard): the camp picture with wind in its leaves.
// Every pixel the wind mask allows (leaves, grass, flowers, far trees) is displaced by layered
// travelling waves; the board, its text, the stool, the sign, the statue and the fence (mask = 0)
// stand still. Two pictures + two masks so the time of day can cross-fade inside one pass.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;       // seconds
    float strength;   // calm sway, in pixels
    float gust;       // 0..1 - a gust passing
    float mixv;       // 0 = picture A, 1 = picture B
    vec2 texel;       // 1 / picture size
};
layout(binding = 1) uniform sampler2D srcA;
layout(binding = 2) uniform sampler2D srcB;
layout(binding = 3) uniform sampler2D maskA;
layout(binding = 4) uniform sampler2D maskB;

vec2 windAt(vec2 uv, float m)
{
    // a gust is a front that travels left -> right across the picture
    float front = smoothstep(0.0, 0.35, gust) * (0.55 + 0.45 * sin(uv.x * 3.1 - time * 2.2));
    float w = sin(time * 1.55 + uv.y * 21.0 + uv.x * 7.0) * 0.55
            + sin(time * 2.70 + uv.y * 43.0 - uv.x * 11.0) * 0.28
            + sin(time * 0.62 + uv.x * 4.0 + uv.y * 3.0) * 0.45
            + sin(time * 5.30 + uv.y * 97.0 + uv.x * 61.0) * 0.12 * (0.4 + front);   // leaf flutter
    float amp = m * (strength + strength * 2.2 * front);
    // the wind blows mostly sideways; leaves bob a little
    return vec2(w + 0.6 * front, 0.35 * sin(time * 2.1 + uv.x * 29.0 + uv.y * 7.0)) * amp * texel;
}

void main()
{
    vec2 uv = qt_TexCoord0;
    float ma = texture(maskA, uv).r;
    float mb = texture(maskB, uv).r;
    vec4 a = texture(srcA, uv + windAt(uv, ma));
    vec4 b = texture(srcB, uv + windAt(uv, mb));
    fragColor = mix(a, b, mixv) * qt_Opacity;
}
