#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex; uniform vec2 texel, direction; varying vec2 vUV;
void main() {
  vec2 d = direction * texel;
  gl_FragColor = texture2D(tex, vUV) * 0.442 + (texture2D(tex, vUV + d) + texture2D(tex, vUV - d)) * 0.279;
}
