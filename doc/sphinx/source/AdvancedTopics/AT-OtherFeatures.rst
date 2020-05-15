..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

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
