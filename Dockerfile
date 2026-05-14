FROM ubuntu:24.04

# installing dependencies
RUN apt-get update --yes
RUN apt-get install --yes \
        git \
        make \
        autoconf \
        automake \
        libtool \
        flex \
        bison \
        cmake \
        g++ \
        gfortran \
        libhdf5-dev \
        libopenblas-dev \
        mpich \
        libmpich-dev \
        libtirpc-dev \
        python3 \
        python3-pip \
        python3-packaging \
        python3-jinja2 \
        python3-yaml \
        python3-pkgconfig \
        curl \
        nano \
        libtbb-dev \
        libglfw3-dev \
        libgl1-mesa-dev

# compile libmesh from source and install it
RUN git clone --recurse-submodules https://github.com/libMesh/libmesh.git /XDG_TEST_SYSTEM/libmesh
WORKDIR /XDG_TEST_SYSTEM/libmesh/build
RUN ../configure --prefix=/XDG_TEST_SYSTEM/libmesh_install_dir
RUN make -j$(nproc)
RUN make install

ENV LIBMESH_INSTALL_PATH=/XDG_TEST_SYSTEM/libmesh_install_dir
ENV PKG_CONFIG_PATH=/XDG_TEST_SYSTEM/libmesh_install_dir/lib/pkgconfig

# build MOAB from source
RUN git clone --recurse-submodules https://bitbucket.org/fathomteam/moab.git /XDG_TEST_SYSTEM/moab
WORKDIR /XDG_TEST_SYSTEM/moab/build
RUN cmake .. \
        -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/moab_install_dir \
        -DENABLE_HDF5=ON \
        -DHDF5_ROOT=/usr \
        -DBLAS_LIBRARIES=/usr/lib/x86_64-linux-gnu/libopenblas.so \
        -DBUILD_SHARED_LIBS=ON
RUN make -j$(nproc)
RUN make install

ENV MOAB_INSTALL_PATH=/XDG_TEST_SYSTEM/moab_install_dir

# build Embree from source
RUN git clone https://github.com/RenderKit/embree.git /XDG_TEST_SYSTEM/embree
WORKDIR /XDG_TEST_SYSTEM/embree/build
RUN cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/embree_install_dir \
        -DEMBREE_ISPC_SUPPORT=OFF \
        -DEMBREE_TUTORIALS=OFF \
        -DEMBREE_STATIC_LIB=OFF
RUN make -j$(nproc)
RUN make install

ENV EMBREE_INSTALL_PATH=/XDG_TEST_SYSTEM/embree_install_dir

# build XDG
RUN git clone --recurse-submodules -j$(nproc) https://github.com/xdg-org/xdg.git /XDG_TEST_SYSTEM/xdg
WORKDIR /XDG_TEST_SYSTEM/xdg/build
RUN cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DXDG_ENABLE_MOAB=ON \
        -DMOAB_DIR=${MOAB_INSTALL_PATH} \
        -DXDG_ENABLE_LIBMESH=ON \
        -DLIBMESH_DIR=${LIBMESH_INSTALL_PATH} \
        -DXDG_LINK_MPI=ON \
        -DXDG_ENABLE_EMBREE=ON \
        -DCMAKE_PREFIX_PATH=${EMBREE_INSTALL_PATH} \
        -DCMAKE_INCLUDE_PATH=${EMBREE_INSTALL_PATH}/include \
        -DXDG_BUILD_TESTS=ON \
        -DXDG_BUILD_TOOLS=ON \

RUN make -j$(nproc)
