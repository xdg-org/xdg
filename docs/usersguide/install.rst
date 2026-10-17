

XDG Installation
================

Prerequisites
-------------

The XDG library requires the following software to be installed on the system:

    - `CMake <https://cmake.org/>`_
    - `C++ compiler <https://gcc.gnu.org/>`_

The current method for XDG installation is to use the CMake build system. For a successful build,
at least one of the following mesh libraries must be installed on the system

    - `libMesh <http://libmesh.github.io/>`_
    - `MOAB <https://sigma.mcs.anl.gov/moab-library/>`_
    - `MFEM <https://mfem.org/>`_


The location(s) of the library installation(s) should be appended to the
``CMAKE_PREFIX_PATH`` variable in the configuration line.

For example, if MOAB is installed in ``$HOME/opt/moab``, the configuration line (as called from the source directory with a build in `build`) would be:

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/opt/moab -DXDG_ENABLE_MOAB=ON

to configure the XDG build to use the MOAB library. If multiple libraries are installed, their paths can be
separated by semicolons, for example:

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/opt/moab;$HOME/opt/libmesh" -DXDG_ENABLE_MOAB=ON -DXDG_ENABLE_LIBMESH=ON

The installation location for XDG is controlled by the ``CMAKE_INSTALL_PREFIX``
variable, which can be set in the configuration line. By default, XDG will be
installed to the systmem location (e.g. ``/usr/local``) which may require
elevated permissions. To install to a user directory, set the prefix to a path
in the user's home directory, for example:

.. code-block:: bash

   cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/opt/moab -DXDG_ENABLE_MOAB=ON -DCMAKE_INSTALL_PREFIX=$HOME/opt/xdg

cuBQL GPU builder
-----------------

With ``XDG_ENABLE_CUBQL=ON``, XDG builds BVHs on the GPU and copies them into
OpenMP-owned storage for traversal. CUDA is the default builder. For AMD GPUs,
set ``CUBQL_USE_HIP=ON`` and ``CMAKE_HIP_ARCHITECTURES`` to the target GPU
architecture. HIP builds require CMake 3.21 or newer and a ROCm HIP toolchain.

The ``cubql_llvm_mi300x`` and ``cubql_llvm_mi355x`` presets select HIP with
``gfx942`` and ``gfx950``, respectively, and set the matching OpenMP offload
flags. For example, with the LLVM OpenMP runtime directory supplied through
``LLVM_OPENMP_RUNTIME_DIR``:

.. code-block:: bash

   cmake --preset cubql_llvm_mi300x -DCMAKE_PREFIX_PATH="$HOME/opt/moab"
   cmake --build --preset cubql_llvm_mi300x

If CMake cannot locate the HIP compiler, set ``CMAKE_HIP_COMPILER`` to the
ROCm Clang compiler (for example, ``/opt/rocm/llvm/bin/clang++``).

Installation
------------

After a successful CMake configuration, the XDG library can be built by running
``make`` and then ``make install`` in the build directory.

Continuing from the example above:

.. code-block:: bash

    make install

Testing
-------

A variety of tests are built automatically. The test set will vary depending on
which mesh libraries are part of the XDG build.

Tests can be run by executing the test suite with the command from the build directory:

.. code-block:: bash

    make test

OR

.. code-block:: bash

    ctest

Mesh Library-Specific Installation Instructions
===============================================

libMesh
=======

By default libMesh is compiled and installed with various levels of optimization
enabled, denoted by the suffixes "opt", "dbg", "devel". When configuring
:term:`XDG` with :term:`libMesh`, the environment variable ``METHOD`` can be set
to one of these values to ensure that the discovered version of libMesh is
compiled with the desired level of optimization. If ``METHOD`` is not set, the
default :term:`libMesh` package configuration will will be used (typically
configured as "opt"). Please refer to the :term:`libMesh` documentation for
the full set of allowed values for ``METHOD``.
