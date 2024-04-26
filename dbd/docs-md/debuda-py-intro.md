# Introduction

Debuda is a silicon debugging tool. It combines the output of Buda backend compiler and the state on the devices
to show and analyze a Buda run.

It can operate in one of the following three modes:

### Built-in Buda Server

   Enable this mode by setting the environment variable: **TT_DEBUG_SERVER_PORT=5555**.

   ![Debuda with Buda built-in server](images/debuda-buda.png){ width=80% }

### Standalone Server

   Use this when Buda terminates unexpectedly. Debuda will spawn and connect to a server automatically.

   ![Debuda with standalone server](images/debuda-debuda-server.png){ width=80% }

<div style="page-break-after: always;"></div>

### Offline Mode

   Ideal for testing or when hardware is unavailable. It uses cached data from a previous Debuda run.

   ![Debuda offline](images/debuda-export-db.png){ width=80% }

<div style="page-break-after: always;"></div>
