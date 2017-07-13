..
   Copyright 2017 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Notifications for dynamically-changing Resources
================================================

Some Resources may represent values that change over time, like sensor readings.
An LwM2M server may be interested in variance of such values and use the Observe
operation to request notifications when they change, or when their value meets
certain criteria.

.. seealso::
    More information about available notification criteria can be found under
    **<NOTIFICATION>** Class Attributes description in :ref:`lwm2m-attributes`.

When some part of the data model changes by means other than LwM2M, one has to
tell the library about it by calling an appropriate function:

- if a Resource value changed - `anjay_notify_changed`,
- if one or more Object Instances were created or removed -
  `anjay_notify_instances_changed`.

Anjay then decides if the notification shall be sent, based on the currently
assigned Attributes (to the part of the data model being changed) and LwM2M
Servers that are interested in seeing the change.

.. note::
    One should not call `anjay_notify_changed`/`anjay_notify_instances_changed`
    when the value change was directly caused by LwM2M (e.g. by Write or Create
    request). Anjay handles these cases internally.

.. seealso::
    Detailed description of these functions can be found in `API docs
    <../../api>`_.

Calling `anjay_notify_changed`/`anjay_notify_instances_changed` does not send
notifications immediately, but schedules a task to be run on next
`anjay_sched_run` call. That way, notifications for multiple values can be
handled as a batch, for example in case where the server observes an entire
Object Instance.

LwM2M attributes
----------------

Correct handling of LwM2M Observe requests requires being able to store
Object/Instance/Resource attributes. For that, one needs to either implement
a set of attribute handlers, or use the pre-defined
:doc:`Attribute Storage module <../../AdvancedTutorial/AT1>`. For simplicity,
we will use the module in this tutorial.

Example
-------

As an example we'll add notification support for the test LwM2M Object defined
in :doc:`BT_CO1_SingleInstanceReadOnly` tutorial. It contains a Timestamp
Resource, whose value changes every second. We need to periodically notify the
library about that fact:

.. highlight:: c
.. snippet-source:: examples/tutorial/custom-object/notifications/src/main.c


    // Wait for the events if necessary, and handle them.
    // ...

    // Notify the library about a Resource value change.
    // Timestamp (Object 1234, Instance 0, Resource 1) value changes each
    // second, and this part of the code is executed roughly every second.
    anjay_notify_changed(anjay, 1234, 0, 1);

    // Finally run the scheduler (ignoring its return value, which
    // is the amount of tasks executed)

One more thing to consider is setting up LwM2M attribute handlers - we'll leave
that to the Attribute Storage module:

.. snippet-source:: examples/tutorial/custom-object/notifications/src/main.c

    // Let the Attribute Storage module implement all attribute handlers.
    // The module is cleaned up automatically in anjay_delete().
    int result = anjay_attr_storage_install(anjay);

    // ...

    if (result) {
        goto cleanup;
    }

That's all you need to make your client support LwM2M Observe/Notify operations!

.. note::

    Complete code of this example can be found in
    `examples/tutorial/custom-object/notifications` subdirectory of main Anjay
    project repository.
