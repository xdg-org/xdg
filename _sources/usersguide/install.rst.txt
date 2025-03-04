

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


Installation
------------

After a successful CMake configuration, the XDG library can be built by running
``make`` and then ``make install`` in the build directory.


Testing
-------

A variety of tests are built automatically. The test set will vary depending on
which mesh libraries are part of the XDG build.

Testing of the XDG library can be done by running the ``ctest`` command in the
build directory.

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