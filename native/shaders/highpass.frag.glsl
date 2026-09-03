#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D tex; uniform float highPassThreshold; varying vec2 vUV;
void main() {
  vec4 c = texture2D(tex, vUV);
  if (c.r < highPassThreshold) c.r = 0.0;
  if (c.g < highPassThreshold) c.g = 0.0;
  if (c.b < highPassThreshold) c.b = 0.0;
  gl_FragColor = c;
}
