Cross-Compiling with Meson
==========================

This guide covers cross-compilation for the Media Library components using the Meson build system.

.. |repo-url| replace:: https://github.com/hailo-ai/hailo-media-library.git

Prerequisites
-------------

1. **Clone the repo** and enter its directory:

   .. code-block:: sh

      git clone |repo-url|
      cd hailo-media-library

2. **Source your Yocto SDK** so that ``CC``, ``CXX``, ``PKG_CONFIG_PATH``, etc. point at the cross toolchain:

   .. code-block:: sh

      source /path/to/yocto-sdk/environment-setup-aarch64-poky-linux

Compiling Hailo Media Library
------------------------------

.. code-block:: sh

   cd hailo-media-library

   # 1. Configure build directory:
   meson setup build \
     --buildtype=debug \
     --prefix=/usr \
     -Dplatform=15h

   # 2. Compile:
   ninja -C build

   # 3. Install to temporary directory:
   DESTDIR=/tmp/install ninja -C build install

   # 4. Copy files to target device:
   scp -r /tmp/install/* root@<target-ip>:/

.. note::
   To switch to a **release** build, use ``--buildtype=release`` instead of ``--buildtype=debug``.

.. note::
   For Hailo-15L, use ``-Dplatform=15l`` instead of ``-Dplatform=15h``.

Available Options
~~~~~~~~~~~~~~~~~

- ``-Dplatform=[15l|15h]`` - Target platform (default: ``15h``)
- ``-Dinclude_unit_tests=[true|false]`` - Include unit tests (default: ``true``)
- ``-Dbuild_docs=[true|false]`` - Build documentation (default: ``false``)
- ``--buildtype=[debug|release]`` - Build type

Compiling Hailo AI Analytics
----------------------------

.. code-block:: sh

   cd hailo-analytics

   # 1. Configure build directory:
   meson setup build \
     --buildtype=debug \
     --prefix=/usr \
     -Dplatform=15h \
     -Dapps_install_dir=/home/root/apps

   # 2. Compile (with job limit to prevent memory exhaustion):
   ninja -C build -j 10 -l 10

   # 3. Install to temporary directory:
   DESTDIR=/tmp/install ninja -C build install

   # 4. Copy files to target device:
   scp -r /tmp/install/* root@<target-ip>:/

.. note::
   The ``-j 10 -l 10`` flags limit both compilation jobs and linker jobs to prevent memory exhaustion during large builds.

Available Options
~~~~~~~~~~~~~~~~~

- ``-Dplatform=[15l|15h]`` - Target platform (default: ``15h``)
- ``-Dapps_install_dir=<path>`` - Installation directory for applications (recommended: ``/home/root/apps``)
- ``-Dbuild_docs=[true|false]`` - Build documentation (default: ``false``)
- ``--buildtype=[debug|release]`` - Build type

Compiling Hailo Postprocess
----------------------------

.. code-block:: sh

   cd hailo-postprocess

   # 1. Configure build directory:
   meson setup build \
     --buildtype=debug \
     --prefix=/usr \
     -Dplatform=15h \
     -Dpost_processes_install_dir=/usr/lib/hailo-post-processes/

   # 2. Compile:
   ninja -C build

   # 3. Install to temporary directory:
   DESTDIR=/tmp/install ninja -C build install

   # 4. Copy files to target device:
   scp -r /tmp/install/* root@<target-ip>:/

Available Options
~~~~~~~~~~~~~~~~~

- ``-Dplatform=[15l|15h]`` - Target platform (default: ``15h``)
- ``-Dpost_processes_install_dir=<path>`` - Installation directory for post-process modules (recommended: ``/usr/lib/hailo-post-processes/``)
- ``--buildtype=[debug|release]`` - Build type

Troubleshooting
---------------

Ninja Permission Issues
~~~~~~~~~~~~~~~~~~~~~~~

After install, the ``.ninja_log`` file may be owned by root. Fix with:

.. code-block:: sh

   sudo chown $(id -u):$(id -g) build/.ninja_log

Memory Issues During Build
~~~~~~~~~~~~~~~~~~~~~~~~~~

If you encounter out-of-memory errors during compilation (especially with Hailo AI Analytics):

.. code-block:: sh

   # Limit parallel jobs
   ninja -C build -j 4 -l 4

   # Or for very constrained systems
   ninja -C build -j 1

Missing Dependencies
~~~~~~~~~~~~~~~~~~~~

If you see errors about missing packages or libraries:

1. Ensure your Yocto SDK is properly sourced
2. Verify that ``PKG_CONFIG_PATH`` points to the toolchain's pkg-config directory
3. Check that required dependencies are installed in the toolchain sysroot

.. code-block:: sh

   # Verify environment
   echo $CC
   echo $CXX
   echo $PKG_CONFIG_PATH

Build Configuration Errors
~~~~~~~~~~~~~~~~~~~~~~~~~~~

If meson reports configuration errors:

1. Check that the platform value is correct (``15l`` or ``15h``)
2. Verify that all required build dependencies are available
3. Review the meson log:

.. code-block:: sh

   cat build/meson-logs/meson-log.txt
