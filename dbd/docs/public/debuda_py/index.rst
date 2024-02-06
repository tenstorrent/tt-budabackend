Debuda.py
=========

Introduction
------------

Debuda.py is a silicon debugger. It combines the output of Buda backend compiler and current state on the devices
to show and analyze the current state of a Buda run.

It can operate in three distinct modes:

1. With the built-in Buda server. Buda can spawn is still running

   To enable this mode, run Buda with the following environment variable set: ``TT_DEBUG_SERVER_PORT=5555``.

.. image:: ../../images/debuda-buda.png
   :width: 500

2. By spawning a standalone server: useful when Buda terminates unexpectedly. Debuda.py will spawn a server
   and connect to it automatically.

.. image:: ../../images/debuda-debuda-server.png
   :width: 500

3. Offline: with no hardware connected. This mode uses data cached from a previous Debuda run. It is useful
   for testing, or when the hardware is not accessible.

.. image:: ../../images/debuda-export-db.png
   :width: 600

Tutorial
--------

The following tutorial will guide you through the basic usage of Debuda.py.

.. toctree::
   :maxdepth: 2

   demo-debuda

Invocation (CLI arguments)
--------------------------

.. argparse::
   :module: debuda
   :func: get_argument_parser
   :prog: debuda

Commands
--------

.. include:: commands.generated-rst
