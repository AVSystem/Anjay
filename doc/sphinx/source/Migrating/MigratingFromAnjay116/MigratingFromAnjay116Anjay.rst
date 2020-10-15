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

Changes in Anjay proper
=======================

.. contents:: :local:

.. highlight:: c

Generic renames
---------------

Some objects (functions, type names, constants) have been renamed with little to
no significant semantic changes:

.. list-table::
   :header-rows: 1

   * - Old identifiers
     - New identifiers and notes
   * - | ``udp_socket_config``
       | (field in ``anjay_configuration_t``)
     - | ``socket_config``
       | Now also used for TCP (in commercial version only)
   * - | ``disable_server_initiated_bootstrap``
       | (field in ``anjay_configuration_t``)
     - | ``disable_legacy_server_initiated_bootstrap``
       | Does not affect LwM2M 1.1-style Server-Initiated Bootstrap (in
         commercial version only)
   * - | ``anjay_new_from_registration_and_observe_persistence()``
       | ``anjay_delete_with_registration_and_observe_persistence()``
     - | ``anjay_new_from_core_persistence()``
       | ``anjay_delete_with_core_persistence()``
       | Available in commercial version only.
       | Includes additional data, such as last queue mode preference.
   * - | ``ANJAY_IID_INVALID``
     - | ``ANJAY_ID_INVALID``
       | Now also relevant for OIDs, RIDs and RIIDs, according to changes
         introduced in LwM2M TS 1.1.
   * - | ``anjay_dm_attributes_t``
     - | ``anjay_dm_oi_attributes_t``
       | Introduced additional fields: ``min_eval_period`` and
         ``max_eval_period``
   * - | ``ANJAY_DM_ATTRIBS_EMPTY``
     - | ``ANJAY_DM_OI_ATTRIBUTES_EMPTY``
   * - | ``anjay_dm_resource_attributes_t``
     - | ``anjay_dm_r_attributes_t``
   * - | ``ANJAY_RES_ATTRIBS_EMPTY``
     - | ``ANJAY_DM_R_ATTRIBUTES_EMPTY``
   * - | ``anjay_udp_security_mode_t``
     - | ``anjay_security_mode_t``
       | Now also used for TCP and NIDD (in commercial version only)
   * - | ``ANJAY_UDP_SECURITY_PSK``
       | ``ANJAY_UDP_SECURITY_RPK``
       | ``ANJAY_UDP_SECURITY_CERTIFICATE``
       | ``ANJAY_UDP_SECURITY_NOSEC``
     - | ``ANJAY_SECURITY_PSK``
       | ``ANJAY_SECURITY_RPK``
       | ``ANJAY_SECURITY_CERTIFICATE``
       | ``ANJAY_SECURITY_NOSEC``

Removed APIs
------------

The following APIs have been completely removed:

* ``ANJAY_SUPPORTED_ENABLER_VERSION`` - previously a constant string hardcoded
  to ``"1.0"``. Currently (in the commercial version), the enabler version used
  may differ depending on the library's compile-time configuration, and runtime
  negotiation with any specific server, so it is pointless to declare it as a
  constant.
* ``anjay/persistence.h`` header - the ``persistence`` module has been
  refactored and moved into ``avs_commons`` beginning with Anjay 1.9, but Anjay
  retained this header that aliased new APIs to their old names. This has been
  completely removed in Anjay 2.x. Please use the ``avs_commons`` APIs directly.
  See also: :ref:`avs-commons-persistence-changes`.

Changes to data model API
-------------------------

Anjay 2.x introduced some major changes to the public data model API. Some of
them have been introduced to allow direct addressing of Resource Instances, as
required by LwM2M TS 1.1. Other changes have been introduced to simplify
handling of Object Instances and Resources, unifying them with the paradigm
introduced for Resource Instances.

Enumerating Object Instances
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``instance_it`` and ``instance_present`` data model handlers (
``anjay_dm_instance_it_t`` and ``anjay_dm_instance_present_t`` function types)
have been replaced with the new ``list_instances`` handler
(``anjay_dm_list_instances_t`` type).

``anjay_dm_emit()`` function is used in the new handler to return existing
instances. It shall be called once for each of the Object Instances existing
in the Object at the time of calling.

Example of old code, taken from the Portfolio object implementation in the demo
application::

    // NOTE: Code compatible with Anjay 1.16

    static int instance_present(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid) {
        (void) anjay;
        return find_instance(get_obj(obj_ptr), iid) != NULL;
    }

    static int instance_it(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t *out,
                           void **cookie) {
        (void) anjay;

        AVS_LIST(portfolio_instance_t) curr =
                (AVS_LIST(portfolio_instance_t)) *cookie;
        if (!curr) {
            curr = get_obj(obj_ptr)->instances;
        } else {
            curr = AVS_LIST_NEXT(curr);
        }

        *out = curr ? curr->iid : ANJAY_IID_INVALID;
        *cookie = curr;
        return 0;
    }

    // ...

    static const anjay_dm_object_def_t OBJ_DEF = {
        // ...
        .handlers = {
            .instance_it = instance_it,
            .instance_present = instance_present,
            // ...
        }
    };

Equivalent new code:

.. snippet-source:: demo/objects/portfolio.c

    static int list_instances(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_dm_list_ctx_t *ctx) {
        (void) anjay;

        AVS_LIST(portfolio_instance_t) it;
        AVS_LIST_FOREACH(it, get_obj(obj_ptr)->instances) {
            anjay_dm_emit(ctx, it->iid);
        }
        return 0;
    }

    // ...

    static const anjay_dm_object_def_t OBJ_DEF = {
        // ...
        .handlers = {
            .list_instances = list_instances,
            // ...
        }
    };

.. important::

    It is REQUIRED that the Object Instances are reported in strictly ascending
    order with regard to the Instance ID, i.e. that the Instance ID passed to
    each consecutive call to ``anjay_dm_emit()`` is higher than the one passed
    to a previous call, if any (within any given call the the ``list_instances``
    handler).

.. note::

    The new ``anjay_dm_list_instances_SINGLE()`` function can be used as a
    generic implementation of the ``list_instances`` handler for Single-Instance
    Objects, much like ``anjay_dm_instance_it_SINGLE()`` and
    ``anjay_dm_instance_present_SINGLE()`` in Anjay 1.x.

Enumerating Resources
^^^^^^^^^^^^^^^^^^^^^

The following entities have been removed:

* ``resource_present`` and ``resource_operations`` handlers in
  ``anjay_dm_handlers_t``
* ``anjay_dm_resource_present_t`` and ``anjay_dm_resource_operations_t``
  function types associated with the above
* ``supported_rids`` field in ``anjay_dm_object_def_t``
* ``anjay_dm_supported_rids_t`` type and ``ANJAY_DM_SUPPORTED_RIDS`` macro,
  associated with the above

Both the "present" and "operations" handlers, as well as the "supported RIDs"
declaration, have been replaced by a single ``list_resources`` handler (and its
associated ``anjay_dm_list_resources_t`` function type).

That handler works in a way very similar to the "list instances" handler
described above. Due to the need to return additional metadata about the
Resources, ``anjay_dm_emit_res()`` is used to return information about supported
Resources.

Example of old code, taken from the Access Control Object implementation::

    // NOTE: Code compatible with Anjay 1.16

    static int ac_resource_present(anjay_t *anjay,
                                   obj_ptr_t obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid) {
        (void) anjay;
        switch (rid) {
        case ANJAY_DM_RID_ACCESS_CONTROL_OID:
        case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
        case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
            return 1;
        case ANJAY_DM_RID_ACCESS_CONTROL_ACL: {
            access_control_instance_t *inst =
                    find_instance(_anjay_access_control_from_obj_ptr(obj_ptr), iid);
            if (inst) {
                return inst->has_acl ? 1 : 0;
            } else {
                return ANJAY_ERR_NOT_FOUND;
            }
        }
        default:
            return 0;
        }
    }

    static int ac_resource_operations(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_rid_t rid,
                                      anjay_dm_resource_op_mask_t *out) {
        (void) anjay;
        (void) obj_ptr;
        *out = ANJAY_DM_RESOURCE_OP_NONE;
        switch (rid) {
        case ANJAY_DM_RID_ACCESS_CONTROL_OID:
        case ANJAY_DM_RID_ACCESS_CONTROL_OIID:
            *out = ANJAY_DM_RESOURCE_OP_BIT_R;
            break;
        case ANJAY_DM_RID_ACCESS_CONTROL_ACL:
        case ANJAY_DM_RID_ACCESS_CONTROL_OWNER:
            *out = ANJAY_DM_RESOURCE_OP_BIT_R | ANJAY_DM_RESOURCE_OP_BIT_W;
            break;
        default:
            return ANJAY_ERR_NOT_FOUND;
        }
        return 0;
    }

    // ...

    static const anjay_dm_object_def_t ACCESS_CONTROL = {
        // ...
        .supported_rids =
                ANJAY_DM_SUPPORTED_RIDS(ANJAY_DM_RID_ACCESS_CONTROL_OID,
                                        ANJAY_DM_RID_ACCESS_CONTROL_OIID,
                                        ANJAY_DM_RID_ACCESS_CONTROL_ACL,
                                        ANJAY_DM_RID_ACCESS_CONTROL_OWNER),
        .handlers = {
            // ...
            .resource_present = ac_resource_present,
            .resource_operations = ac_resource_operations,
            // ...
        }
    };

Equivalent new code:

.. snippet-source:: src/modules/access_control/anjay_access_control_handlers.c

    static int ac_list_resources(anjay_t *anjay,
                                 obj_ptr_t obj_ptr,
                                 anjay_iid_t iid,
                                 anjay_dm_resource_list_ctx_t *ctx) {
        (void) anjay;
        access_control_instance_t *inst =
                find_instance(_anjay_access_control_from_obj_ptr(obj_ptr), iid);

        anjay_dm_emit_res(ctx, ANJAY_DM_RID_ACCESS_CONTROL_OID, ANJAY_DM_RES_R,
                          ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, ANJAY_DM_RID_ACCESS_CONTROL_OIID, ANJAY_DM_RES_R,
                          ANJAY_DM_RES_PRESENT);
        anjay_dm_emit_res(ctx, ANJAY_DM_RID_ACCESS_CONTROL_ACL, ANJAY_DM_RES_RWM,
                          (inst && inst->has_acl) ? ANJAY_DM_RES_PRESENT
                                                  : ANJAY_DM_RES_ABSENT);
        anjay_dm_emit_res(ctx, ANJAY_DM_RID_ACCESS_CONTROL_OWNER, ANJAY_DM_RES_RW,
                          ANJAY_DM_RES_PRESENT);
        return 0;
    }

    // ...

    static const anjay_dm_object_def_t ACCESS_CONTROL = {
        // ...
        .handlers = {
            // ...
            .list_resources = ac_list_resources,
            // ...
        }
    };

.. important::

    It is REQUIRED that the Resources are reported in strictly ascending order
    with regard to the Resource ID, i.e. that the Resource ID passed to each
    consecutive call to ``anjay_dm_emit_res()`` is higher than the one passed to
    a previous call, if any (within any given call the the ``list_resources``
    handler).

.. important::

    The distinction between "supported" and "present" Resources is retained,
    even though the notion of "supported Resources" is no longer directly used
    in code.

    * **Supported Resources**, previously declared through the
      ``supported_rids`` field, are now expressed as the set of Resources for
      which ``anjay_dm_emit_res()`` is called with any arguments.
    * **Present Resources**, previously reported using the ``resource_present``
      handler, are now expressed by passing either ``ANJAY_DM_RES_PRESENT`` or
      ``ANJAY_DM_RES_ABSENT`` as the argument to ``anjay_dm_emit_res()``.

    Write handler is guaranteed to be called only on *supported* Resources, but
    can be called regardless of whether a given Resource is *present*. This is
    the most important rationale behind those being separate concepts.

.. rubric:: Additional changes

The ``anjay_dm_resource_present_TRUE()`` function has been removed without any
replacement. To achieve the same effect, implement the ``list_resources``
handler in a way that always passes ``ANJAY_DM_RES_PRESENT`` as argument to
``anjay_dm_emit_res()``.

The ``anjay_dm_resource_op_bit_t`` bit-masked type has been replaced by stricter
``anjay_dm_resource_kind_t`` enumeration. Below is the table of equivalent
values:

+---------------------------------------------+--------------------------------------------+
| ``anjay_dm_resource_op_bit_t`` (Anjay 1.16) | ``anjay_dm_resource_kind_t`` (Anjay 2.x)   |
+=============================================+============================================+
| | ``ANJAY_DM_RESOURCE_OP_BIT_R``            | | ``ANJAY_DM_RES_R`` (single-instance)     |
|                                             | | ``ANJAY_DM_RES_RM`` (multiple-instance)  |
+---------------------------------------------+--------------------------------------------+
| | ``ANJAY_DM_RESOURCE_OP_BIT_W``            | | ``ANJAY_DM_RES_W`` (single-instance)     |
|                                             | | ``ANJAY_DM_RES_WM`` (multiple-instance)  |
+---------------------------------------------+--------------------------------------------+
| | ``ANJAY_DM_RESOURCE_OP_BIT_R              | | ``ANJAY_DM_RES_RW`` (single-instance)    |
|   | ANJAY_DM_RESOURCE_OP_BIT_W``            | | ``ANJAY_DM_RES_RWM`` (multiple-instance) |
+---------------------------------------------+--------------------------------------------+
| | ``ANJAY_DM_RESOURCE_OP_BIT_E``            | | ``ANJAY_DM_RES_E``                       |
+---------------------------------------------+--------------------------------------------+
| | ``0`` (empty bit mask)                    | | ``ANJAY_DM_RES_BS_RW``                   |
+---------------------------------------------+--------------------------------------------+

.. note::

    It is now required to pass some ``anjay_dm_resource_kind_t`` value, as it is
    a required argument to ``anjay_dm_emit_res()``. Please report appropriate
    values for each Resource when migrating. You can consult the `OMA LwM2M
    Registry <http://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html>`_
    if the Object you are migrating is listed there.

Multiple-instance Resource handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Due to the new requirements in LwM2M TS 1.1, Resource Instances are now
addressable individually in Anjay. For this reason, the following array-based
multiple-instance Resource handling functions have been removed:

* ``anjay_ret_array_start()``
* ``anjay_ret_array_index()``
* ``anjay_ret_array_finish()``
* ``anjay_get_array()``
* ``anjay_get_array_index()``
* ``ANJAY_GET_INDEX_END`` macro related to the above

Also removed is the ``resource_dim`` handler, as well as its associated
``anjay_dm_resource_dim_t`` function type and ``ANJAY_DM_DIM_INVALID`` constant.

Instead, the following mechanisms have been introduced:

* New ``list_resource_instances`` handler (and its associated
  ``anjay_dm_list_resource_instances_t`` function type), analogous to the
  ``list_instances`` handler described above, has been introduced.
* ``resource_read`` and ``resource_write`` handlers (associated function types:
  ``anjay_dm_resource_read_t``, ``anjay_dm_resource_write_t``) now take an
  additional ``riid`` argument and are now called separately for each Resource
  Instance.
* New ``resource_reset`` handler (and its associated
  ``anjay_dm_resource_reset_t`` function type), somewhat analogous to the
  ``instance_reset`` handler has been introduced. Its job is to remove all
  Resource Instances from a multiple-instance Resource.

Example of old code, taken from the Portfolio object implementation in the demo
application::

    // NOTE: Code compatible with Anjay 1.16

    static int resource_read(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_output_ctx_t *ctx) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY: {
            anjay_output_ctx_t *array = anjay_ret_array_start(ctx);
            if (!array) {
                return ANJAY_ERR_INTERNAL;
            }
            int result = 0;

            AVS_STATIC_ASSERT(_MAX_IDENTITY_TYPE <= UINT16_MAX,
                              identity_type_too_big);
            for (int32_t i = 0; i < _MAX_IDENTITY_TYPE; ++i) {
                if (!inst->has_identity[i]) {
                    continue;
                }

                if ((result = anjay_ret_array_index(array, (anjay_riid_t) i))
                        || (result = anjay_ret_string(array,
                                                      inst->identity_value[i]))) {
                    return result;
                }
            }
            return anjay_ret_array_finish(array);
        }
        default:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

    static int resource_write(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_input_ctx_t *ctx) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY: {
            anjay_input_ctx_t *array = anjay_get_array(ctx);
            if (!array) {
                return ANJAY_ERR_INTERNAL;
            }

            anjay_riid_t riid;
            int result = 0;
            char value[MAX_IDENTITY_VALUE_SIZE];
            memset(inst->has_identity, 0, sizeof(inst->has_identity));
            while (!result && !(result = anjay_get_array_index(array, &riid))) {
                result = anjay_get_string(array, value, sizeof(value));

                if (riid >= _MAX_IDENTITY_TYPE) {
                    return ANJAY_ERR_BAD_REQUEST;
                }

                inst->has_identity[riid] = true;
                strcpy(inst->identity_value[riid], value);
            }
            if (result && result != ANJAY_GET_INDEX_END) {
                return ANJAY_ERR_BAD_REQUEST;
            }
            return 0;
        }

        default:
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

    static int resource_dim(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY: {
            int dim = 0;
            for (int32_t i = 0; i < _MAX_IDENTITY_TYPE; ++i) {
                dim += !!inst->has_identity[i];
            }
            return dim;
        }
        default:
            return ANJAY_DM_DIM_INVALID;
        }
    }

    // ...

    static const anjay_dm_object_def_t OBJ_DEF = {
        // ...
        .handlers = {
            // ...
            .resource_read = resource_read,
            .resource_write = resource_write,
            .resource_dim = resource_dim,
            // ...
        }
    };

Equivalent new code:

.. snippet-source:: demo/objects/portfolio.c

    static int resource_read(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_riid_t riid,
                             anjay_output_ctx_t *ctx) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY:
            assert(riid < _MAX_IDENTITY_TYPE);
            assert(inst->has_identity[riid]);
            return anjay_ret_string(ctx, inst->identity_value[riid]);
        default:
            AVS_UNREACHABLE("Read called on unknown resource");
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

    static int resource_write(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid,
                              anjay_riid_t riid,
                              anjay_input_ctx_t *ctx) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY: {
            if (riid >= _MAX_IDENTITY_TYPE) {
                return ANJAY_ERR_NOT_FOUND;
            }
            char value[MAX_IDENTITY_VALUE_SIZE];
            int result = anjay_get_string(ctx, value, sizeof(value));
            if (!result) {
                inst->has_identity[riid] = true;
                strcpy(inst->identity_value[riid], value);
            }
            return result;
        }

        default:
            AVS_UNREACHABLE("Write called on unknown resource");
            return ANJAY_ERR_METHOD_NOT_ALLOWED;
        }
    }

    static int resource_reset(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid,
                              anjay_rid_t rid) {
        (void) anjay;
        (void) rid;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        assert(rid == RID_IDENTITY);
        memset(inst->has_identity, 0, sizeof(inst->has_identity));
        return 0;
    }

    static int list_resource_instances(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid,
                                       anjay_rid_t rid,
                                       anjay_dm_list_ctx_t *ctx) {
        (void) anjay;

        portfolio_t *obj = get_obj(obj_ptr);
        assert(obj);
        portfolio_instance_t *inst = find_instance(obj, iid);
        assert(inst);

        switch (rid) {
        case RID_IDENTITY: {
            for (anjay_riid_t i = 0; i < _MAX_IDENTITY_TYPE; ++i) {
                if (inst->has_identity[i]) {
                    anjay_dm_emit(ctx, i);
                }
            }
            return 0;
        }
        default:
            AVS_UNREACHABLE(
                    "Attempted to list instances in a single-instance resource");
            return ANJAY_ERR_INTERNAL;
        }
    }

    // ...

    static const anjay_dm_object_def_t OBJ_DEF = {
        // ...
        .handlers = {
            // ...
            .resource_read = resource_read,
            .resource_write = resource_write,
            .resource_reset = resource_reset,
            .list_resource_instances = list_resource_instances,
            // ...
        }
    };

.. important::

    It is REQUIRED that the Resource Instances are reported in strictly
    ascending order with regard to the Resource Instance ID, i.e. that the
    Resource Instance ID passed to each consecutive call to ``anjay_dm_emit()``
    is higher than the one passed to a previous call, if any (within any given
    call the the ``list_resource_instances`` handler).

.. note::

    In the example above, there is only one multiple-instance Resource in the
    object. Please note that if there are single-instance Resources in the same
    object as well, the same ``resource_read`` and ``resource_write`` handlers
    are used for both single- and multiple-instance Resources.

    When value of single-instance Resource is being read or written to, the
    ``riid`` argument is passed ``ANJAY_ID_INVALID`` (65535).

Simplified Instance Create handler
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The function type associated with the ``instance_create`` handler has previously
been declared as::

    typedef int
    anjay_dm_instance_create_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t *inout_iid,
                               anjay_ssid_t ssid);

This has been simplified into:

.. snippet-source:: include_public/anjay/dm.h

    typedef int
    anjay_dm_instance_create_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid);

There are two important differences:

* The Instance ID is now always assigned either by the LwM2M Server or by Anjay;
  the user code does not need to assign it any more, hence the ``iid`` parameter
  is no longer an "inout pointer".

* The ``ssid`` argument has been removed. It was introduced as a hack to make
  implementing the Access Control object possible - the internal implementation
  of the Access Control mechanism has been overhauled so that it is no longer
  necessary.

  * If you are writing your own implementation of the Access Control object, you
    may assume the the creation has been initiated by the Bootstrap Server
    (``ssid == ANJAY_SSID_BOOTSTRAP``).

Refactored execute argument value getter
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``anjay_execute_get_arg_value()`` function has been refactored in connection
to :ref:`ssize-t-removal-in-commons-116`.

- **Old API:**
  ::

      ssize_t anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                          char *out_buf,
                                          size_t buf_size);

- **New API:**

  .. snippet-source:: include_public/anjay/io.h
     :emphasize-lines: 1-2

      int anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                      size_t *out_bytes_read,
                                      char *out_buf,
                                      size_t buf_size);

- Return value semantics have been aligned with those of ``anjay_get_string()``
  - 0 is returned for success, negative value for error, or
  ``ANJAY_BUFFER_TOO_SHORT`` (1) if the buffer was too small.

  Length of the extracted data, which is equivalent to the old return value
  semantics, can be retrieved using the new ``out_bytes_read`` argument, but it
  can also be ``NULL`` if that is not necessary.


New error handling scheme
-------------------------

.. note::

    See also the chapter regarding ``avs_commons``:
    :ref:`avs-commons-new-error-handling`

List of functions and callback function types that changed return value from
``int`` to ``avs_error_t``, without any other signature changes:

* ``anjay_bootstrapper()`` (available in commercial version only)
* ``anjay_access_control_persist()``
* ``anjay_access_control_restore()``
* ``anjay_attr_storage_persist()``
* ``anjay_attr_storage_restore()``
* ``anjay_download_next_block_handler_t`` (callback passed as ``on_next_block``
  field in ``anjay_download_config_t``)
* ``anjay_security_persist()``
* ``anjay_security_restore()``
* ``anjay_server_persist()``
* ``anjay_server_restore()``

Additional changes related to error handling:

* **Scheduler runner**

  * **Old API:**
    ::

        int anjay_sched_run(anjay_t *anjay);

  * **New API:**

    .. snippet-source:: include_public/anjay/core.h

        void anjay_sched_run(anjay_t *anjay);

  * Errors are no longer reported, as their meaning and possible approaches to
    error handling were extremely vague. Please treat ``anjay_sched_run()`` as
    always succeeding.

* **Downloader function**

  * **Old API:**
    ::

        anjay_download_handle_t anjay_download(anjay_t *anjay,
                                               const anjay_download_config_t *config);

  * **New API:**

    .. snippet-source:: include_public/anjay/download.h

        avs_error_t anjay_download(anjay_t *anjay,
                                   const anjay_download_config_t *config,
                                   anjay_download_handle_t *out_handle);

  * The download handle is returned via an additional output argument. The
    return value contains a detailed error code.


* **Download finished handler**

  * **Old API:**
    ::

        typedef void
        anjay_download_finished_handler_t(anjay_t *anjay, int result, void *user_data);

  * **New API:**

    .. snippet-source:: include_public/anjay/download.h

        typedef struct {
            anjay_download_result_t result;

            union {
                /**
                 * Error code. Only valid if result is ANJAY_DOWNLOAD_ERR_FAILED.
                 *
                 * Possible values include (but are not limited to):
                 *
                 * - <c>avs_errno(AVS_EADDRNOTAVAIL)</c> - DNS resolution failed
                 * - <c>avs_errno(AVS_ECONNABORTED)</c> - remote resource is no longer
                 *   valid
                 * - <c>avs_errno(AVS_ECONNREFUSED)</c> - server responded with a reset
                 *   message on the application layer (e.g. CoAP Reset)
                 * - <c>avs_errno(AVS_ECONNRESET)</c> - connection lost or reset
                 * - <c>avs_errno(AVS_EINVAL)</c> - could not parse response from the
                 *   server
                 * - <c>avs_errno(AVS_EIO)</c> - internal error in the transfer code
                 * - <c>avs_errno(AVS_EMSGSIZE)</c> - could not send or receive datagram
                 *   because it was too large
                 * - <c>avs_errno(AVS_ENOMEM)</c> - out of memory
                 * - <c>avs_errno(AVS_ETIMEDOUT)</c> - could not receive data from
                 *   server in time
                 */
                avs_error_t error;

                /**
                 * Protocol-specific status code. Only valid if result is
                 * ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE.
                 *
                 * Currently it may be a HTTP status code (e.g. 404 or 501), or a CoAP
                 * code (e.g. 132 or 161 - these examples are canonically interpreted as
                 * 4.04 and 5.01, respectively). If any user log is to depend on status
                 * codes, it is expected that it will be interpreted in line with the
                 * URL originally passed to @ref anjay_download for the same download.
                 */
                int status_code;
            } details;
        } anjay_download_status_t;
        // ...
        typedef void anjay_download_finished_handler_t(anjay_t *anjay,
                                                       anjay_download_status_t status,
                                                       void *user_data);

  * The vague ``int result`` argument, that could be passed several different
    kinds of values, has been replaced with a more descriptive and
    better-defined ``anjay_download_status_t`` structure.

Updated security configuration for downloads
--------------------------------------------

A new type called ``anjay_security_config_t`` has been introduced, used in the
downloader component in places where Anjay 1.x used ``avs_net_security_info_t``.
This has been done to enable configuration of (D)TLS ciphersuites in addition to
the values previously available.

The new type is declared as follows:

.. snippet-source:: include_public/anjay/core.h

    typedef struct {
        /**
         * DTLS keys or certificates.
         */
        avs_net_security_info_t security_info;

        /**
         * Single DANE TLSA record to use for certificate verification, if
         * applicable.
         */
        const avs_net_socket_dane_tlsa_record_t *dane_tlsa_record;

        /**
         * TLS ciphersuites to use.
         *
         * A value with <c>num_ids == 0</c> (default) will cause defaults configured
         * through <c>anjay_configuration_t::default_tls_ciphersuites</c>
         * to be used.
         */
        avs_net_socket_tls_ciphersuites_t tls_ciphersuites;
    } anjay_security_config_t;

As described in the documentation cited above, it is safe to only use the
``security_info`` field and zero out the rest of the structure.

The following APIs are affected by the change:

* **Security-related field in** ``anjay_download_config_t``

  * **Old API:**
    ::

        typedef struct anjay_download_config {
            // ...
            /**
             * DTLS keys or certificates. Required if coaps:// is used,
             * ignored for coap:// transfers.
             */
            avs_net_security_info_t security_info;
            // ...
        } anjay_download_config_t;

  * **New API:**

    .. snippet-source:: include_public/anjay/download.h

        typedef struct anjay_download_config {
            // ...
            /**
             * DTLS security configuration. Required if coaps:// is used,
             * ignored for coap:// transfers.
             *
             * Contents of any data aggregated as pointers within is copied as needed,
             * so it is safe to free all related resources array after the call to
             * @ref anjay_download.
             */
            anjay_security_config_t security_config;
            // ...
        } anjay_download_config_t;

  * Aside from updating the type, the field name has been changed to
    ``security_config``.

* **Security information getter field in** ``anjay_fw_update_handlers_t``

  * **Old API:**
    ::

        typedef int
        anjay_fw_update_get_security_info_t(void *user_ptr,
                                            avs_net_security_info_t *out_security_info,
                                            const char *download_uri);

        // ...
        typedef struct {
            // ...
            /** Queries security information that shall be used for an encrypted
             * connection; @ref anjay_fw_update_get_security_info_t */
            anjay_fw_update_get_security_info_t *get_security_info;
            // ...
        } anjay_fw_update_handlers_t;

  * **New API:**

    .. snippet-source:: include_public/anjay/fw_update.h

        typedef int anjay_fw_update_get_security_config_t(
                void *user_ptr,
                anjay_security_config_t *out_security_info,
                const char *download_uri);

        // ...
        typedef struct {
            // ...
            /** Queries security configuration that shall be used for an encrypted
             * connection; @ref anjay_fw_update_get_security_config_t */
            anjay_fw_update_get_security_config_t *get_security_config;
            // ...
        } anjay_fw_update_handlers_t;

  * Aside from updating the argument type, both the field and the getter
    function type have been renamed to use the word ``config`` instead of
    ``info``.

* **Getter function for retrieving security information from data model**

  * **Old API:**
    ::

        avs_net_security_info_t *anjay_fw_update_load_security_from_dm(anjay_t *anjay,
                                                                       const char *uri);

  * **New API:**

    .. snippet-source:: include_public/anjay/core.h

        int anjay_security_config_from_dm(anjay_t *anjay,
                                          anjay_security_config_t *out_config,
                                          const char *raw_url);

  * The equivalent function is now declared in ``anjay/core.h`` instead of
    ``anjay/fw_update.h``, and has a different signature.

    The security configuration is now returned through an output argument with
    any necessary internal buffers cached inside the Anjay object instead of
    using heap allocation. Please refer to the Doxygen-based documenation of
    this function for details.

Changes to CMake option variables
---------------------------------

The following CMake options have been renamed or replaced with others with
similar semantics:

+-------------------------------------------------+---------------------------------------------------+
| Old variable                                    | New variable and notes                            |
+=================================================+===================================================+
| | ``WITH_BLOCK_DOWNLOAD``                       | | ``WITH_COAP_DOWNLOAD``                          |
|                                                 | | Part of the new ``avs_coap`` component          |
+-------------------------------------------------+---------------------------------------------------+
| | ``WITH_BLOCK_RECEIVE``                        | | ``WITH_AVS_COAP_BLOCK`` (for both directions)   |
| | ``WITH_BLOCK_SEND``                           | | Part of the new ``avs_coap`` component          |
+-------------------------------------------------+---------------------------------------------------+
| | ``WITH_JSON``                                 | | ``WITH_LWM2M_JSON``                             |
|                                                 | | Refers to the legacy LwM2M 1.0 JSON format only |
+-------------------------------------------------+---------------------------------------------------+
| | ``WITH_REGISTRATION_AND_OBSERVE_PERSISTENCE`` | | ``WITH_CORE_PERSISTENCE``                       |
|                                                 | | Available in commercial version only            |
+-------------------------------------------------+---------------------------------------------------+

Additionally, the following variables have been removed:

  * ``MAX_FLOAT_STRING_SIZE`` - ``double`` is now always used when stringifying
  * ``MAX_OBSERVABLE_RESOURCE_SIZE`` - no hard limit is imposed in the current
    version
  * ``WITH_ATTR_STORAGE`` - removed due to overlapping semantics with
    ``WITH_MODULE_attr_storage``
  * ``WITH_AVS_COAP_MESSAGE_CACHE`` - message cache is always enabled in the
    current version

Other changes
-------------

Declaration of ``anjay_smsdrv_cleanup()`` (only relevant for the commercial
version) has been moved from ``anjay/core.h`` to ``anjay/sms.h``.
