// Nirvana Desktop - screen savers: animations drawn with FLTK
//
// Used full screen by nirvana-lock and small in Preferences > Screen Saver (the preview).
// Each saver draws the whole area in draw(); step() moves the animation on by dt seconds.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace savers {

class Saver {
public:
  virtual ~Saver() = default;
  virtual void resize(int W, int H) { w = W; h = H; }
  virtual void step(double dt) = 0;
  virtual void draw(int X, int Y) = 0;   // the area X, Y, w, h
  int w = 0, h = 0;
};

// id and the name in the interface language ("blank" first, "random" last)
std::vector<std::pair<std::string, std::string>> list();
std::string name(const std::string &id);
// "random" picks one of the others (not "blank")
Saver *make(const std::string &id, int W, int H);

}  // namespace savers
