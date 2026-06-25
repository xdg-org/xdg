FROM ubuntu:24.04

ARG BUILD_JOBS=1

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
        curl \
        nano \
        libembree-dev \
        pkg-config \
        libnetcdf-dev

# compile libmesh from source and install it
RUN git clone --recurse-submodules https://github.com/libMesh/libmesh.git /XDG_TEST_SYSTEM/libmesh
WORKDIR /XDG_TEST_SYSTEM/libmesh/build
RUN ../configure --prefix=/XDG_TEST_SYSTEM/libmesh_install_dir
RUN make -j ${BUILD_JOBS}
RUN make install

ENV LIBMESH_INSTALL_PATH=/XDG_TEST_SYSTEM/libmesh_install_dir

# build MOAB from source
RUN git clone --recurse-submodules https://bitbucket.org/fathomteam/moab.git /XDG_TEST_SYSTEM/moab
WORKDIR /XDG_TEST_SYSTEM/moab/build
RUN cmake .. \
        -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/moab_install_dir \
        -DENABLE_HDF5=ON \
        -DHDF5_ROOT=/usr \
        -DBLAS_LIBRARIES=/usr/lib/x86_64-linux-gnu/libopenblas.so \
        -DBUILD_SHARED_LIBS=ON
RUN make -j${BUILD_JOBS}
RUN make install

ENV MOAB_INSTALL_PATH=/XDG_TEST_SYSTEM/moab_install_dir

# installing SEACAS to enable exodus support
RUN git clone --depth=1 https://github.com/sandialabs/seacas.git /XDG_TEST_SYSTEM/seacas

WORKDIR /XDG_TEST_SYSTEM/seacas/build

RUN cmake .. \
    -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/seacas_install_dir \
    -DCMAKE_BUILD_TYPE=Release \
    -DSeacas_ENABLE_ALL_PACKAGES=OFF \
    -DSeacas_ENABLE_SEACASExodus=ON \
    -DTPL_ENABLE_MPI=OFF \
    -DTPL_ENABLE_Netcdf=ON \
    -DTPL_Netcdf_INCLUDE_DIRS=/usr/include \
    -DTPL_Netcdf_LIBRARIES=/usr/lib/x86_64-linux-gnu/libnetcdf.so

RUN make -j${BUILD_JOBS}
RUN make install
ENV SEACAS_INSTALL_PATH=/XDG_TEST_SYSTEM/seacas_install_dir

# build omega_h
RUN git clone https://github.com/SCOREC/omega_h.git /XDG_TEST_SYSTEM/omega_h
WORKDIR /XDG_TEST_SYSTEM/omega_h/build
RUN cmake .. \
    -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/omega_h_install_dir \
     -DCMAKE_BUILD_TYPE=Release \
     -DOmega_h_USE_MPI=OFF \
     -DBUILD_TESTING=OFF \
     -DOmega_h_USE_SEACASExodus=ON 

RUN make -j ${BUILD_JOBS}
RUN make install
ENV OMEGA_H_INSTALL_PATH=/XDG_TEST_SYSTEM/omega_h_install_dir



# build XDG
RUN git clone --recurse-submodules https://github.com/xdg-org/xdg.git /XDG_TEST_SYSTEM/xdg
WORKDIR /XDG_TEST_SYSTEM/xdg/build
RUN cmake .. \
        -DCMAKE_INSTALL_PREFIX=/XDG_TEST_SYSTEM/xdg_install_dir \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=mpicc \
        -DCMAKE_CXX_COMPILER=mpicxx \
        -DXDG_ENABLE_MOAB=ON \
        -DMOAB_DIR=${MOAB_INSTALL_PATH} \
        -DXDG_ENABLE_LIBMESH=ON \
        -DLIBMESH_DIR=${LIBMESH_INSTALL_PATH} \
        -DXDG_ENABLE_OMEGA_H=ON \
        -DOMEGA_H_dir=${OMEGA_H_INSTALL_PATH}

RUN make -j${BUILD_JOBS}
RUN make install

ENV XDG_INSTALL_PATH=/XDG_TEST_SYSTEM/xdg_install_dir
