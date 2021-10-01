..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

:orphan:

Event loop has been refactored
==============================

Starting with Anjay 2.14.0, a new event loop API has been introduced.

If an external site has linked you to this document, please contact its
administrator so that the relevant links are updated.

You may be interested in one of the following documents:

* :doc:`BC-Initialization` tutorial, which now includes a call to the new event
  loop API
* API documentation for `anjay_event_loop_run()
  <../api/core_8h.html#a95c229caf3ee8ce7de556256f4307507>`_
* :doc:`../AdvancedTopics/AT-CustomEventLoop`, the updated tutorial from
  previous versions that has now been moved to the *Advanced topics* section,
  as it is generally no longer required to implement the event loop in basic
  applications
