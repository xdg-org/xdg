#ifndef OVERLAP_H
#define OVERLAP_H

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "xdg/mesh_manager_interface.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

using namespace xdg;

using OverlapMap = std::map<std::set<int>, Position>;
using ElementVertices = std::vector<xdg::Vertex>;

struct EdgeRayQuery {
    Position origin; // ray_fire() launch origin
    Direction direction; // ray_fire() launch direction
    double edgeLength; // length of edge used as max distance in ray_fire()
};

// check mesh manager instance for overlaps
void check_instance_for_overlaps(std::shared_ptr<XDG> xdg,
                                 OverlapMap& overlap_map,
                                 bool checkEdges,
                                 bool verboseOutput);

void report_overlaps(const OverlapMap& overlap_map);

std::vector<EdgeRayQuery> return_ray_queries(const ElementVertices &tri);

MeshID check_along_edge(std::shared_ptr<XDG> xdg,
                        std::shared_ptr<MeshManager> mm,
                        const EdgeRayQuery& rayquery,
                        const std::vector<MeshID>& volsToCheck,
                        std::vector<Position>& edgeOverlapLocs);
#endif // include guard
