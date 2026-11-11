// Grayscale: no colors at all.
void main() {
  float l = dot(screen(v_texcoord).rgb, vec3(0.299, 0.587, 0.114));
  gl_FragColor = vec4(vec3(l), 1.0);
}
