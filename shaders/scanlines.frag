// Scanlines: only the dark lines of a CRT, the picture stays flat and sharp.
void main() {
  vec3 col = screen(v_texcoord).rgb;
  col *= 0.86 + 0.14 * sin(gl_FragCoord.y * 3.14159);
  gl_FragColor = vec4(col * 1.08, 1.0);
}
