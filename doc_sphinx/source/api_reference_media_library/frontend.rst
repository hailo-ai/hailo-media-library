.. _frontend-label:

=============
Frontend
=============

C++ Configuration API
---------------------

The Frontned can be configured using the following C++ API:

example:

.. code-block:: c++


    auto config_expected = frontend->get_config();
    if (!config_expected)
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

    frontend_config_t config = config_expected.value();

    config.frontned_element_config.ldc_config.dewarp_config.enabled = false;
    if (m_media_lib->frontend->set_config(config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }


For more information about the configuration structs see the `frontend_config_t` struct in :doc:`media_library_types` documentation.


API Reference
-------------

.. doxygentypedef:: FrontendWrapperCallback
   :project: media_library

.. doxygenclass:: MediaLibraryFrontend
   :project: media_library
   :members:

.. doxygentypedef:: MediaLibraryFrontendPtr
   :project: media_library

