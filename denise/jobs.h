// Nirvana Desktop - Rambler: file operations in the background (copy, move, delete...)
//
// Every operation is a job with its own thread: jobs run side by side, independent of each
// other, and the windows keep working. A job counts its bytes and files, can be paused and
// canceled; the "Jobs" window shows them all (it opens by itself when a job takes longer
// than a moment). The thread never touches FLTK: when the job ends, its on_done runs in the
// interface thread (refreshing the windows, reporting errors).
#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace jobs {

struct Job {
  std::string title;
  std::atomic<uint64_t> done{0}, total{0};
  std::atomic<int> files_done{0}, files_total{0};
  std::atomic<bool> cancel{false}, pause{false}, finished{false};
  std::function<void(Job &)> work;       // in the job's thread
  std::function<void(Job &)> on_done;    // in the interface thread, after the work
  double started = 0;
  // filled by the work, read after it (or under the lock)
  std::mutex m;
  std::string current, errors;
  std::vector<std::pair<std::string, std::string>> moved;   // (from, to) - windows follow
  std::vector<std::string> made;                            // created files
  void set_current(const std::string &s) { std::lock_guard<std::mutex> l(m); current = s; }
  void error(const std::string &e) { std::lock_guard<std::mutex> l(m); errors += e + "\n"; }
  bool check();   // waits while paused; false: canceled
};

void start(std::shared_ptr<Job> j);
int running();             // how many jobs are not finished
void show_window();        // the Jobs window

// For the work functions:
uint64_t measure(const std::filesystem::path &p, int &files);   // bytes and files of a tree
// copy a file or a tree (symbolic links as links), counting bytes; false and err on error/cancel
bool copy_tree(Job &j, const std::filesystem::path &src, const std::filesystem::path &dst, std::string &err);
// move: rename, or copy and delete across file systems
bool move_tree(Job &j, const std::filesystem::path &src, const std::filesystem::path &dst, std::string &err);
bool remove_tree(Job &j, const std::filesystem::path &p, std::string &err);

}  // namespace jobs
