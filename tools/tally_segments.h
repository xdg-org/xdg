#include <memory>
#include <string>
#include <vector>
#include <numeric>
#include <functional>
#include <omp.h>

#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/timer.h"
#include "xdg/bbox.h"

#include "xdg/xdg.h"

using namespace xdg;

struct TallyContext {
  std::shared_ptr<XDG> xdg_;
  int n_threads_ {1};
  int n_tracks_ {0};
  bool check_tracks_ {false};
  bool verbose_ {false};
  bool quiet_ {false};
  std::function<void(int, int, size_t)> on_track_complete_ {};
};

void tally_segments(const TallyContext& context) {
  const auto& xdg = context.xdg_;

  // get the bounding box of the mesh
  BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
  std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << "\n";

  #ifdef XDG_OPENMP
    omp_set_num_threads(context.n_threads_);
    std::cout << fmt::format("Using {} threads", context.n_threads_) << "\n";
  #else
    if (context.n_threads_ != 1) {
      std::cout << "Warning: OpenMP not enabled; running in single-threaded mode\n";
    }
  #endif

  int n_tracks_run = 0;

  Timer timer;
  timer.start();
  #pragma omp parallel shared(n_tracks_run)
  {
    #pragma omp for
    for (int i = 0; i < context.n_tracks_; i++) {
      // sample a location within the bounding box
      Position r1 = bbox.sample_location();
      if (!bbox.contains(r1)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r1));

      Position r2 = bbox.sample_location();
      if (!bbox.contains(r2)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r2));

      auto segments = xdg->segments(r1, r2);
      #pragma omp atomic
      n_tracks_run++;

      if (context.on_track_complete_) {
        #pragma omp critical
        context.on_track_complete_(i, n_tracks_run, segments.size());
      }

      if (context.quiet_) continue;

      if (context.check_tracks_) {
        double track_length = (r2 - r1).length();
        double segement_sum = std::accumulate(segments.begin(), segments.end(), 0.0, [](double v, const auto& s) { return v + s.second; });
        double diff = fabs(track_length - segement_sum);
        if (diff > TINY_BIT) {
          fatal_error(fmt::format("Track length check failed.\n Start: {}\n End: {}\n Diff: {}", r1, r2, diff));
        }
      }
    }
  }
  timer.stop();

  std::cout << fmt::format("Time elapsed: {} s", timer.elapsed()) << "\n";
}
