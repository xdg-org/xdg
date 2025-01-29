.. _xdg_intro:

Introduction to XDG
===================

XDG is an open-source ray tracing toolkit providing robust ray tracing on
discretized geometry models for particle transport operations in scientific
simulations. Namely, XDG supports ray tracing operations on models composed of
surfaces of triangles, blocks of tetrahedra, or combinations thereof. It is
primarily designed for use with the open-source Monte Carlo particle transport
code :term:`OpenMC`.

XDG is intended for use in simulations where the geometry is complex and
the particle transport problem is computationally expensive. XDG is designed
to be fast, memory-efficient, and scalable to large problems.

XDG is also meant to act as a common harness for unstructured mesh libraries
that OpenMC commonly operates with for analysis and research purposes. Those
libraries include:

    - `libMesh <http://libmesh.github.io/>`_
    - `MOAB <https://sigma.mcs.anl.gov/moab-library/>`_
    - `MFEM <https://mfem.org/>`_

