#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D pyr_0, pyr_1, pyr_2, pyr_3, pyr_4; uniform float bloomStrength; varying vec2 vUV;
void main() {
  vec4 total = vec4(0.);
  total += texture2D(pyr_0, vUV) * 0.96549;
  total += texture2D(pyr_1, vUV) * 0.92832;
  total += texture2D(pyr_2, vUV) * 0.88790;
  total += texture2D(pyr_3, vUV) * 0.84343;
  total += texture2D(pyr_4, vUV) * 0.79370;
  gl_FragColor = total * bloomStrength;
}
