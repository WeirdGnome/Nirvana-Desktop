// Large pixels: the screen in 4x4 blocks.
void main() {
  const float size = 4.0;
  vec2 p = (floor(gl_FragCoord.xy / size) * size + size * 0.5) / u_resolution;
  gl_FragColor = vec4(screen(p).rgb, 1.0);
}
