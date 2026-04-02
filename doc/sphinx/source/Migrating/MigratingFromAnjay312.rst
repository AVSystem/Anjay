..
   Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 3.11 or 3.12
=================================

.. contents:: :local:

.. highlight:: c

Addition of resource_instance_remove handler
--------------------------------------------

LwM2M TS 1.2 introduced possibility to delete a Resource Instance, so
one additional data model handler had to be added.

New ``resource_instance_remove`` handler (and its associated
``anjay_dm_resource_instance_remove_t`` function type) has been introduced. It
is analogous to the ``instance_remove`` handler; its job is to remove a specific
Resource Instance from a multiple-instance Resource.

Its implementation is required in objects that include at least one writeable
multiple-instance Resource, if the client application aims for compliance with
LwM2M 1.2.
