#ifndef XDG_OMEGA_H_MESH_MANAGER_H
#define XDG_OMEGA_H_MESH_MANAGER_H


#include "xdg/mesh_manager_interface.h"


namespace xdg{

class OmegaHMeshManager:public MeshManager{

public:
    OmegaHMeshManager();
    ~OmegaHMeshManager()=default;

    void load_file(const std::string& file_path) override;

    void init() override;

    int num_volumes() const override;
    int num_surfaces() const override;

    int num_ents_of_dimension(int dim) const;


    MeshID create_volume() override;

    void add_surface_to_volume(MeshID volume, MeshID surface, Sense sense, bool overwrite=false) override;

    // Mesh
    int num_volume_elements(MeshID volume) const override;

    int num_volume_elements() const override;

    int num_volume_faces(MeshID volume) const override;

    int num_surface_faces(MeshID surface) const override;

    int num_vertices() const override;

    std::vector<MeshID> get_volume_elements(MeshID volume) const override;

    std::vector<MeshID> get_surface_faces(MeshID surface) const override;

    std::vector<MeshID> element_connectivity(MeshID element) const override;

    std::vector<MeshID> face_connectivity(MeshID face) const override;

    Vertex vertex_coordinates(MeshID vertex) const override;

    std::vector<Vertex> element_vertices(MeshID element) const override;

    std::array<Vertex, 3> face_vertices(MeshID face) const override;

    SurfaceElementType get_surface_element_type(MeshID surface) const override;

    MeshID adjacent_element(MeshID element, int face) const override;

    double element_volume(MeshID element) const override;

    // Topology
    std::pair<MeshID, MeshID> surface_senses(MeshID surface) const override;

    std::vector<MeshID> get_volume_surfaces(MeshID volume) const override;

    Sense surface_sense(MeshID surface, MeshID volume) const override;

    // Metadata
    void parse_metadata() override;

};


}
#endif //XDG_MESH_MANAGER_H
