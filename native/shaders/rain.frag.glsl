#extension GL_OES_standard_derivatives : enable
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define PI 3.14159265359
#define SQRT_2 1.4142135623730951
#define SQRT_5 2.23606797749979

uniform vec2 resolution;
uniform float time;               // already scaled by animationSpeed
uniform float numColumns;
uniform float fallSpeed, raindropLength, cycleSpeed;
uniform float baseContrast, baseBrightness;
uniform float glyphSequenceLength, glyphEdgeCrop, msdfPxRange;
uniform vec2 glyphTextureGridSize, glyphMSDFSize;
uniform sampler2D glyphMSDF;

highp float randomFloat(const in vec2 uv) {
  const highp float a = 12.9898, b = 78.233, c = 43758.5453;
  highp float dt = dot(uv.xy, vec2(a, b)), sn = mod(dt, PI);
  return fract(sin(sn) * c);
}
float hash13(vec3 p3) {
  p3 = fract(p3 * 0.1031);
  p3 += dot(p3, p3.zyx + 31.32);
  return fract((p3.x + p3.y) * p3.z);
}
float wobble(float x) { return x + 0.3 * sin(SQRT_2 * x) + 0.2 * sin(SQRT_5 * x); }

// The heart of the effect: a wobbling sawtooth per column, brightest at the tooth's tip.
float getRainBrightness(float simTime, vec2 glyphPos) {
  float columnTimeOffset = randomFloat(vec2(glyphPos.x, 0.)) * 1000.;
  float columnSpeedOffset = randomFloat(vec2(glyphPos.x + 0.1, 0.)) * 0.5 + 0.5;
  float columnTime = columnTimeOffset + simTime * fallSpeed * columnSpeedOffset;
  float rainTime = (glyphPos.y * 0.01 + columnTime) / raindropLength;
  return 1.0 - fract(wobble(rainTime));
}
float median3(vec3 i) { return max(min(i.r, i.g), min(max(i.r, i.g), i.b)); }

void main() {
  // Square cells, numColumns of them along the longer screen axis, grid centred on screen.
  vec2 uv = (gl_FragCoord.xy - 0.5 * resolution) / max(resolution.x, resolution.y) + 0.5;
  vec2 cellF = uv * numColumns;
  vec2 cell = floor(cellF);
  vec2 glyphPos = cell + 0.5;

  // Raindrop brightness and cursor detection
  float brightness = getRainBrightness(time, glyphPos);
  float brightnessBelow = getRainBrightness(time, glyphPos + vec2(0., -1.));
  bool isCursor = brightness > brightnessBelow;
  float base = brightness * baseContrast + baseBrightness;

  // Glyph choice: each cell cycles at cycleSpeed with its own phase, picking a random symbol each time
  float phase = hash13(vec3(cell, 7.0));
  float cycle = floor(phase + time * cycleSpeed * 60.0);
  float symbol = floor(glyphSequenceLength * hash13(vec3(cell, mod(cycle, 1024.0) + 0.5)));
  symbol = clamp(symbol, 0.0, glyphSequenceLength - 1.0);

  // Resolve the fragment to its position in the MSDF glyph atlas
  vec2 guv = fract(cellF) - 0.5;
  guv *= clamp(1. - glyphEdgeCrop, 0., 1.);
  guv += 0.5;
  float symbolX = mod(symbol, glyphTextureGridSize.x);
  float symbolY = glyphTextureGridSize.y - (symbol - symbolX) / glyphTextureGridSize.x - 1.;
  guv = (guv + vec2(symbolX, symbolY)) / glyphTextureGridSize;

  vec2 unitRange = vec2(msdfPxRange) / glyphMSDFSize;
  vec2 screenTexSize = vec2(1.0) / fwidth(guv);
  float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
  float signedDistance = median3(texture2D(glyphMSDF, guv).rgb);
  float glyph = clamp(screenPxRange * (signedDistance - 0.5) + 0.5, 0.0, 1.0);

  // R: ordinary glyph brightness, G: cursor brightness (coloured separately later)
  vec2 rg = (isCursor ? vec2(0.0, 1.0) : vec2(1.0, 0.0)) * base;
  gl_FragColor = vec4(rg * glyph, 0.0, 1.0);
}
