#include <memory>
#include <string>
#include <vector>
#include <numeric>

#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

using namespace xdg;

Position sample_box_location(const BoundingBox& bbox) {
  return bbox.lower_left() + bbox.width() * Vec3da(drand48(), drand48(), drand48());
}

struct WalkElementsContext {
  std::shared_ptr<XDG> xdg_;
  double mean_free_path_;
  size_t n_particles_;
};

void walk_elements(const WalkElementsContext& context) {
  auto xdg = context.xdg_;
  double mean_free_path = context.mean_free_path_;

  // get the bounding box of the mesh
  BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
  std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << std::endl;

  MeshID element = ID_NONE;
  Position r;
  int n_events {0};
  double distance {0.0};
  double total_distance {0.0};
  for (int i = 0; i < context.n_particles_; i++) {
    n_events = 0;
    distance = 0.0;
    element = ID_NONE;

    // sample a location within the model
    while (element == ID_NONE) {
      r = sample_box_location(bbox);
      element = xdg->find_element(r);
    }

    Direction u = rand_dir();
    u.normalize();
    std::vector<MeshID> primitives;
    while (element != ID_NONE) {
      // determine the distace to the next element
      auto [next_element, exit_distance] = xdg->next_element(element, r, u);

      // determine the distance to the next collision
      double collision_distance = -std::log(1.0 - drand48()) * mean_free_path;

      if (collision_distance < exit_distance) {
        r += u * collision_distance;
        distance += collision_distance;
        // simulate an isotropic collision
        u = rand_dir();
      } else {
        r += u * exit_distance;
        distance += exit_distance;
        // if the next element isn't present, move on to the next particle
        element = next_element;
      }

      // attempt to trace the mesh boundary for re-entrance
      while (element == ID_NONE) {
        auto ray_hit = xdg->ray_fire(xdg->mesh_manager()->implicit_complement(), r, u, INFTY, HitOrientation::EXITING, &primitives);
        // if there is no re-entry point, move on to the next particle
        if (ray_hit.second == ID_NONE) break;

        // move ray up to surface
        r += u * ray_hit.first;
        distance += ray_hit.first;
        // TODO: if we intersected a corner, we might not find an element
        element = xdg->find_element(r+ u*TINY_BIT);
      }
      primitives.clear();
      n_events++;
    }
    total_distance += distance;
    std::cout << fmt::format("Particle {} underwent {} events. Distance: {}", i, n_events, distance) << std::endl;
  }

  std::cout << fmt::format("Average distance: {}", total_distance/context.n_particles_) << std::endl;

}