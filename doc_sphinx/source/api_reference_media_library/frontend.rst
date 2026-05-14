.. _frontend-label:

=============
Frontend
=============

C++ Configuration API
---------------------

The frontend is configured through the ``MediaLibrary`` profile API. To modify frontend settings at runtime,
get the current profile, modify it, and apply the override:

.. code-block:: c++

    auto profile_expected = media_library->get_current_profile();
    auto profile = profile_expected.value();

    profile.iq_settings.dewarp.enabled = false;
    if (media_library->set_override_parameters(profile) != MEDIA_LIBRARY_SUCCESS)
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

