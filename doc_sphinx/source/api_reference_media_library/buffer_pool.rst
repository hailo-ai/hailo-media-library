.. _bufferpool-label:

===========
Buffer Pool
===========

Overview
========

A buffer pool is a technique used to manage memory efficiently by pre-allocating a pool (or a collection) of memory buffers of fixed size. Instead of allocating and deallocating memory dynamically each time a buffer is needed or released, the buffer pool pre-allocates these buffers once and manages them using acquire and release operations.

How Does it work?
------------------

1. Initialization:
   - During initialization or setup phase, a buffer pool is created.
   - It allocates a fixed number of memory buffers, all of the same size.
2. Acquiring Buffers:
   - When a component or module needs a buffer, it requests one from the buffer pool using an acquire operation.
   - The buffer pool keeps track of which buffers are currently in use and which are free.
3. Releasing Buffers:
   - After a component finishes using a buffer, it releases it back to the buffer pool using a free operation.
   - The buffer pool marks the buffer as free and makes it available for reuse.
4. Efficiency:
   - Since buffers are pre-allocated and reused, the overhead of frequent dynamic memory allocation and deallocation is reduced.
   - This approach can improve performance by reducing memory fragmentation and minimizing the time spent in memory allocation routines.

Benefits
--------

1. Reduced Overhead: Avoids frequent calls to new and delete , which can be costly.
2. Improved Performance: Faster allocation and deallocation times due to preallocation and reuse.
3. Control Over Memory Usage: Allows better control over how memory is allocated and used within an application.


Buffer pools are particularly useful in scenarios where memory allocation and deallocation operations are frequent and need to be efficient, such as in networking applications, databases, or multimedia processing systems.


MediaLibraryBufferPool API Guide
================================

The `MediaLibraryBufferPool` class provides a buffer pool for managing media buffers. This guide explains how to use the main functions of the `MediaLibraryBufferPool` API: `init`, `free`, `acquire_buffer`, and `release_buffer`.

.. note:: The buffers in the ``MediaLibraryBufferPool`` are allocated from a physically contiguous memory region. This ensures better performance and compatibility with hardware accelerators.

Initialization
--------------

.. doxygenfunction:: MediaLibraryBufferPool::init
   :project: media_library


Deinitialization (Free)
-----------------------

.. doxygenfunction:: MediaLibraryBufferPool::free
   :project: media_library


Acquire Buffer
--------------

.. doxygenfunction:: MediaLibraryBufferPool::acquire_buffer
   :project: media_library


Release Buffer
--------------

.. doxygenfunction:: MediaLibraryBufferPool::release_buffer
   :project: media_library


Usage Example
-------------

.. doxygenclass:: MediaLibraryBufferPool
    :project: media_library
