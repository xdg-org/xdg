.. XDG documentation master file, created by
   sphinx-quickstart. You can adapt this file
   completely to your liking, but it should at least
   contain the root `toctree` directive.

XDG: Accelerated Discrete Geometry for Particle Transport
=========================================================

XDG is an open-source ray tracing toolkit providing robust ray tracing
for particle transport operations in scientific simulations.

XDG stands on the shoulders of the `Direct Accelerated Geometry Monte Carlo
(DAGMC) <https://svalinn.github.io/DAGMC/index.html>`_ toolkit and it's
development team, from which it draws significant guidance and inspiration. The
intent of this library is to provide expanded DAGMC functionality across
multiple mesh libraries and formats.  Various members of the DAGMC development
team are collaborators and contributors to the XDG project.

.. .. carousel::
..     :show_controls:
..     :show_fade:
..     :show_captions_below:

..     .. figure:: assets/manifold-cad.png
..        :height: 400px

..        CAD

..        A pipe manifold modeled in :term:`Coreform Cubit`.

..     .. figure:: assets/manifold-tris.png
..        :height: 400px

..        Surface Mesh w/ Embedded Topology

..        A DAGMC surface mesh of the piping manifold.

..     .. figure:: assets/manifold-flux.png
..        :height: 400px

..        Flux Results

..        A flux mapping generated using :term:`OpenMC`.

Table of Contents
=================

.. toctree::
    :maxdepth: 1

    intro/index
    usersguide/index
    api/index
    methods/index
    glossary