#include "xdg/embree/ray_tracer.h"
#include "xdg/error.h"
#include "xdg/geometry_data.h"
#include "xdg/tetrahedron_contain.h"
#include "xdg/ray.h"

namespace xdg {

void error(void* dum, RTCError code, const char* str) {
  if (code != RTC_ERROR_NONE)
    fatal_error("Embree error: {}", str);
}

EmbreeRayTracer::EmbreeRayTracer()
{
  device_ = rtcNewDevice(nullptr);
  rtcSetDeviceErrorFunction(device_, (RTCErrorFunction)error, nullptr);
}

EmbreeRayTracer::~EmbreeRayTracer()
{
  rtcReleaseDevice(device_);
}

void EmbreeRayTracer::init()
{

}

RTCScene EmbreeRayTracer::create_embree_scene() {
  RTCScene rtcscene = rtcNewScene(device_);
  rtcSetSceneFlags(rtcscene, RTC_SCENE_FLAG_ROBUST);
  rtcSetSceneBuildQuality(rtcscene, RTC_BUILD_QUALITY_HIGH);
  return rtcscene;
}

std::pair<TreeID, TreeID>
EmbreeRayTracer::register_volume(const std::shared_ptr<MeshManager> mesh_manager,
                           MeshID volume_id)
{

  TreeID faces_tree = create_surface_tree(mesh_manager, volume_id);

  // set up point location tree for any volumetric elements
  TreeID element_tree = create_element_tree(mesh_manager, volume_id);

  return {faces_tree, element_tree};
}

TreeID EmbreeRayTracer::create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                                            MeshID volume)
{
  TreeID tree = next_tree_id();
  trees_.push_back(tree);
  auto volume_scene = this->create_embree_scene();

  // allocate storage for this volume
  auto volume_faces = mesh_manager->get_volume_faces(volume);
  this->primitive_ref_storage_[volume_scene].resize(volume_faces.size());
  auto& triangle_storage = this->primitive_ref_storage_[volume_scene];

  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume);
  int storage_offset {0};
  for (auto& surface_id : volume_surfaces) {
    // get the sense of this surface with respect to the volume
    Sense triangle_sense {Sense::UNSET};
    auto surf_to_vol_senses = mesh_manager->get_parent_volumes(surface_id);
    if (volume == surf_to_vol_senses.first) triangle_sense = Sense::FORWARD;
    else if (volume == surf_to_vol_senses.second) triangle_sense = Sense::REVERSE;
    else fatal_error("Volume {} is not a parent of surface {}", volume, surface_id);

    auto surface_faces = mesh_manager->get_surface_faces(surface_id);
    for (int i = 0; i < surface_faces.size(); ++i) {
      auto& primitive_ref = triangle_storage[i + storage_offset];
      primitive_ref.primitive_id = surface_faces[i];
      primitive_ref.sense = triangle_sense;
    }
    storage_offset += surface_faces.size();
 }

  PrimitiveRef* tri_ref_ptr = triangle_storage.data();

  auto bump = bounding_box_bump(mesh_manager, volume);
/*
  TODO: none of the above is ray tracer specific. This can be a part of the common register_volume
  implementation. Then another virtual function can be called register_volume_RT_specific.
  in this scenario register_volume isn't a virtual function but instead calls a virtual function.
  That virtual function will be overrided to do the RT specific things in registering the volume.
  Primitive_ref_storage is the only non-local variable used in this context that is a member of the derived class.
  However when implementing GPRT the above may need to be written differently with GPU buffers in mind.
  So we will leave it as is for now.
*/

  // create a new geometry for each surface
  int buffer_start = 0;
  for (auto surface : volume_surfaces) {
    auto surface_triangles = mesh_manager->get_surface_faces(surface);
    RTCGeometry surface_geometry = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_USER);
    rtcSetGeometryUserPrimitiveCount(surface_geometry, surface_triangles.size());
    unsigned int embree_surface = rtcAttachGeometry(volume_scene, surface_geometry);
    this->surface_to_geometry_map_[surface] = surface_geometry;

    std::shared_ptr<GeometryUserData> surface_data = std::make_shared<GeometryUserData>();
    surface_data->box_bump = bump;
    surface_data->surface_id = surface;
    surface_data->mesh_manager = mesh_manager.get();
    surface_data->prim_ref_buffer = tri_ref_ptr + buffer_start;
    user_data_map_[surface_geometry] = surface_data;

    // TODO: This could be a problem if user_data_map_ is reallocated?
    rtcSetGeometryUserData(surface_geometry, user_data_map_[surface_geometry].get());

    for (int i = 0; i < surface_triangles.size(); ++i) {
      auto& triangle_ref = surface_data->prim_ref_buffer[i];
    }
    buffer_start += surface_triangles.size();

    // set the bounds function
    rtcSetGeometryBoundsFunction(surface_geometry, (RTCBoundsFunction)&TriangleBoundsFunc, nullptr);
    rtcSetGeometryIntersectFunction(surface_geometry, (RTCIntersectFunctionN)&TriangleIntersectionFunc);
    rtcSetGeometryOccludedFunction(surface_geometry, (RTCOccludedFunctionN)&TriangleOcclusionFunc);

    rtcCommitGeometry(surface_geometry);
  }
  rtcCommitScene(volume_scene);
  tree_to_scene_map_[tree] = volume_scene;
  return tree;
}

TreeID EmbreeRayTracer::create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                                            MeshID volume)
{
  auto volume_elements = mesh_manager->get_volume_elements(volume);
  if (volume_elements.size() == 0) return TREE_NONE;

  // create a new geometry
  RTCScene volume_element_scene = create_embree_scene();
  // create primitive references for the volumetric elements
  this->primitive_ref_storage_[volume_element_scene].resize(volume_elements.size());
  auto& volume_element_storage = this->primitive_ref_storage_[volume_element_scene];
  for (int i = 0; i < volume_elements.size(); ++i) {
    auto& primitive_ref = volume_element_storage[i];
    primitive_ref.primitive_id = volume_elements[i];
    primitive_ref.sense = Sense::FORWARD;
  }

  RTCGeometry element_geometry = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_USER);
  rtcSetGeometryUserPrimitiveCount(element_geometry, volume_elements.size());
  unsigned int embree_geometry = rtcAttachGeometry(volume_element_scene, element_geometry);
  std::shared_ptr<VolumeElementsUserData> volume_elements_data = std::make_shared<VolumeElementsUserData>();
  volume_elements_data->volume_id = volume;
  volume_elements_data->mesh_manager = mesh_manager.get();
  volume_elements_data->prim_ref_buffer = volume_element_storage.data();
  this->volume_element_user_data_map_[element_geometry] = volume_elements_data;

  rtcSetGeometryUserData(element_geometry, volume_elements_data.get());

  rtcSetGeometryBoundsFunction(element_geometry, (RTCBoundsFunction)&VolumeElementBoundsFunc, nullptr);
  rtcSetGeometryIntersectFunction(element_geometry, (RTCIntersectFunctionN)&TetrahedronIntersectionFunc);
  rtcSetGeometryOccludedFunction(element_geometry, (RTCOccludedFunctionN)&TetrahedronOcclusionFunc);

  rtcCommitGeometry(element_geometry);
  rtcCommitScene(volume_element_scene);

  TreeID tree = next_tree_id();
  trees_.push_back(tree);
  tree_to_scene_map_[tree] = volume_element_scene;
  return tree;
}

MeshID EmbreeRayTracer::find_element(TreeID tree,
                                     const Position& point) const
{

  if (!tree_to_scene_map_.count(tree)) {
    warning("Tree {} does not have a point location tree", tree);
    return ID_NONE;
  }

  RTCScene scene = tree_to_scene_map_.at(tree);

  RTCElementRay ray;
  ray.set_org(point);
  ray.set_dir({1.0, 0.0, 0.0});
  ray.set_tfar(0.0);
  ray.set_tnear(0.0);

  // fire an occlusion ray
  {
    rtcOccluded1(scene, (RTCRay*)&ray);
  }

  if (ray.dtfar != -INFTY) return ID_NONE;

  return ray.element;
}

bool EmbreeRayTracer::point_in_volume(TreeID tree,
                                const Position& point,
                                const Direction* direction,
                                const std::vector<MeshID>* exclude_primitives) const
{
  RTCScene scene = tree_to_scene_map_.at(tree);
  RTCDRayHit rayhit; // embree specfic rayhit struct (payload?)
  rayhit.ray.set_org(point);
  if (direction != nullptr) rayhit.ray.set_dir(*direction);
  else rayhit.ray.set_dir({1. / std::sqrt(2.0), 1 / std::sqrt(2.0), 0.0});
  rayhit.ray.rf_type = RayFireType::VOLUME;
  rayhit.ray.orientation = HitOrientation::ANY;
  rayhit.ray.set_tfar(INFTY);
  rayhit.ray.set_tnear(0.0);

  if (exclude_primitives != nullptr) rayhit.ray.exclude_primitives = exclude_primitives;

  {
    rtcIntersect1(scene, (RTCRayHit*)&rayhit);
  }

  // if the ray hit nothing, the point is outside of the volume
  if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) return false;

  // use the hit triangle normal to determine if the intersection is
  // exiting or entering
  return rayhit.ray.ddir.dot(rayhit.hit.dNg) > 0.0;
}

std::pair<double, MeshID>
EmbreeRayTracer::ray_fire(TreeID tree,
                    const Position& origin,
                    const Direction& direction,
                    const double dist_limit,
                    HitOrientation orientation,
                    std::vector<MeshID>* const exclude_primitves)
{
  RTCScene scene = tree_to_scene_map_.at(tree);
  RTCDRayHit rayhit;
  // set ray data
  rayhit.ray.set_org(origin);
  rayhit.ray.set_dir(direction);
  rayhit.ray.set_tfar(dist_limit);
  rayhit.ray.set_tnear(0.0);
  rayhit.ray.rf_type = RayFireType::VOLUME;
  rayhit.ray.orientation = orientation;
  rayhit.ray.mask = -1; // no mask
  if (exclude_primitves != nullptr) rayhit.ray.exclude_primitives = exclude_primitves;

  // fire the ray
  {
    rtcIntersect1(scene, (RTCRayHit*)&rayhit);
    // TODO: I don't quite understand this...
    rayhit.hit.Ng_x *= -1.0;
    rayhit.hit.Ng_y *= -1.0;
    rayhit.hit.Ng_z *= -1.0;
  }

  if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    return {INFTY, ID_NONE};
  else
    if (exclude_primitves) exclude_primitves->push_back(rayhit.hit.primitive_ref->primitive_id);
    return {rayhit.ray.dtfar, rayhit.hit.surface};
}

void EmbreeRayTracer::closest(TreeID tree,
                        const Position& point,
                        double& distance,
                        MeshID& triangle)
{
  RTCScene scene = tree_to_scene_map_.at(tree);
  RTCDPointQuery query;
  query.set_point(point);

  RTCPointQueryContext context;
  rtcInitPointQueryContext(&context);

  rtcPointQuery(scene, &query, &context, (RTCPointQueryFunction)&TriangleClosestFunc, &scene);

  if (query.geomID == RTC_INVALID_GEOMETRY_ID) {
    distance = INFTY;
    return;
  }

  distance = query.dradius;
  triangle = query.primitive_ref->primitive_id;
}

void EmbreeRayTracer::closest(TreeID scene,
                        const Position& point,
                        double& distance)
{
  MeshID triangle;
  closest(scene, point, distance, triangle);
}

bool EmbreeRayTracer::occluded(TreeID tree,
                         const Position& origin,
                         const Direction& direction,
                         double& distance) const
{
  RTCScene scene = tree_to_scene_map_.at(tree);
  RTCSurfaceRay ray;
  ray.set_org(origin);
  ray.set_dir(direction);
  ray.set_tfar(INFTY);
  ray.set_tnear(0.0);
  ray.rf_type = RayFireType::FIND_VOLUME;
  ray.orientation = HitOrientation::ANY;
  ray.flags = 0;
  ray.mask = -1; // no mask

  // fire the ray
  {
    rtcOccluded1(scene, (RTCRay*)&ray);
  }

  distance = ray.dtfar;
  return distance != INFTY;
}

} // namespace xdg