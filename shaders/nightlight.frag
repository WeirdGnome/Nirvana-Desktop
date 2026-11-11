// Night light: warmer colors with less blue light, for the evening.
void main() {
  vec3 c = screen(v_texcoord).rgb;
  gl_FragColor = vec4(c * vec3(1.0, 0.84, 0.60), 1.0);
}
