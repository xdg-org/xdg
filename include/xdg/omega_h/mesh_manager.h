#ifndef XDG_OMEGA_H_MESH_MANAGER_H
#define XDG_OMEGA_H_MESH_MANAGER_H


#include "mesh_manager_interface.h"

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

};


}
#endif //XDG_MESH_MANAGER_H
