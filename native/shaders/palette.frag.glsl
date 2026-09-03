#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define PI 3.14159265359
uniform sampler2D tex, bloomTex, paletteTex;
uniform float ditherMagnitude, time, cursorIntensity;
uniform vec3 cursorColor;
varying vec2 vUV;
highp float rand(const in vec2 uv, const in float t) {
  const highp float a = 12.9898, b = 78.233, c = 43758.5453;
  highp float dt = dot(uv.xy, vec2(a, b)), sn = mod(dt, PI);
  return fract(sin(sn) * c + t);
}
void main() {
  vec4 brightness = texture2D(tex, vUV) + texture2D(bloomTex, vUV);
  brightness -= rand(gl_FragCoord.xy, time) * ditherMagnitude / 3.0;   // hides banding
  gl_FragColor = vec4(
    texture2D(paletteTex, vec2(brightness.r, 0.0)).rgb
      + min(cursorColor * cursorIntensity * brightness.g, vec3(1.0)),
    1.0);
}
