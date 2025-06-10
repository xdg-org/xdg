.. _api_rti:

XDG Interface
=============

.. doxygenfile:: xdg.h
   :project: XDG

.. doxygenfile:: mesh_manager_interface.h
   :project: XDG

.. doxygenfile:: ray_tracing_interface.h
   :project: XDG

.. doxygenfile:: embree/ray_tracer.h
   :project: XDG

.. doxygenfile:: ray.h
   :project: XDG

----

Ray Tracing Concepts
====================

XDG makes use of an explicit ray tracing interface which can be instantiated with different Ray Tracing Engines. The default is Intel's CPU 
ray tracer Embree. However, GPU support is also available through GPRT which makes use of Vulkan-RT for a vendor-agnostic GPU ray tracing experience.
The different RT library implementations will make reference to different RT library specific types, a definition of which can be found below.

Volume
------
A volume in XDG can be referenced by an internal type ``xdg::MeshID`` but in a ray tracing context a volume is defined as a Top Level Acceleration Structure (TLAS) which 
contains instances of the Bottom Level Acceleration Structures (BLAS) for each surface in the volume. XDG also makes use of a ``xdg::TreeID`` to reference
the TLAS structures created by each respective ray tracing engine.

- XDG: ``MeshID``
- Embree: ``RTCScene``
- GPRT: ``GPRTAccel``

Surface
-------

- XDG: ``MeshID``
- Embree: ``RTCGeometry`` 
- GPRT: ``GPRTGeometry``
