..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Other library features
======================

.. _coap-pull-download:

CoAP PULL download
------------------

If the LwM2M Client needs to download a large file from an external CoAP server,
it may use the `anjay_download API <../api/download_8h.html>`_. The built-in
downloader supports CoAP, CoAP/DTLS and HTTP(S) connections and is able to
perform transfers without interrupting regular LwM2M operations.

For a simple example, see `examples/tutorial/AT-Downloader` subdirectory of main
Anjay project repository.
