// Inverted colors: light text on a dark background and the other way round.
void main() {
  gl_FragColor = vec4(1.0 - screen(v_texcoord).rgb, 1.0);
}
