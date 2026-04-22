Media Library Logger
====================

The Media Library Logger is a simple logger designed to log messages to various destinations using the `spdlog <https://github.com/gabime/spdlog>`_ library.

It offers two distinct logging outputs:

Console Logging
---------------

Messages are logged to the console. By default, the console log level is set to "error." The console log level can be modified using the environment variable ``MEDIALIB_CONSOLE_LOG_LEVEL``.
Example:

.. code-block:: sh

   export MEDIALIB_CONSOLE_LOG_LEVEL="debug"

File Logging
------------

Messages are logged to a local log file named ``medialib.log``. This log file is created in the current working directory. The default log level for the local log file is "info". The log level for this file can be changed using the environment variable ``MEDIALIB_LOG_LEVEL``.
Location of this file can be set by using environment variable ``MEDIALIB_LOGGER_PATH``. 
By default, this is a rotating log file. To make it non-rotating, set ``MEDIALIB_LOGGER_ROTATE`` environment variable to "false".
When used in rotating mode, the maximum file size can be set by using ``MEDIALIB_LOGGER_FILE_SIZE``. Default file size is set to 1MB (1048576).
*Warning:* When ``MEDIALIB_LOGGER_ROTATE`` is set to "false", there is no size limit to the log file.
Example:

.. code-block:: sh

    export MEDIALIB_LOG_LEVEL="error"
    export MEDIALIB_LOGGER_FILE_SIZE="4194304" # 4MB
    export MEDIALIB_LOGGER_ROTATE="false"


Log Levels
^^^^^^^^^^

* trace
* debug
* info
* warn
* error
* critical
* off

Module Specific Logs
^^^^^^^^^^^^^^^^^^^^

Both console and file loggings can receive log levels for specific modules in their environment variables.
Examples:

.. code-block:: sh

   export MEDIALIB_CONSOLE_LOG_LEVEL="warn,denoise=info"

.. code-block:: sh

   export MEDIALIB_LOG_LEVEL="dewarp=off,buffer_pool=debug"


Disable Logs
^^^^^^^^^^^^

In order to disable all media-library logs:

.. code-block:: sh

    export MEDIALIB_LOGGER_PATH=NONE
    export MEDIALIB_LOG_LEVEL=off
    export MEDIALIB_CONSOLE_LOG_LEVEL=off