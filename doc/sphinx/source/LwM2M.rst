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

OMA LwM2M - Brief description
=============================

.. contents:: :local:

`Lightweight Machine to Machine
<https://www.omaspecworks.org/what-is-oma-specworks/iot/lightweight-m2m-lwm2m/>`_
is a protocol developed by the `Open Mobile Alliance
<https://openmobilealliance.org/>`_ for remote device management in the Internet
of Things and other Machine-to-Machine applications.

It is designed to be transported either over UDP secured with DTLS in IP
networks, or over SMS directly in cellular phone networks. The application data
is encapsulated using the
`Constrained Application Protocol <https://tools.ietf.org/html/rfc7252>`_
(CoAP). CoAP is an application layer protocol similar to HTTP in philosophy and
general semantics, but designed specifically with being lightweight in mind.
CoAP makes it possible to transmit messages with low overhead - the minimal
header size is just 4 bytes, which makes it feasible to send meaningful content
within single, non-fragmented UDP datagrams, even over links with low MTU, such
as SMS.

Anjay is designed to hide most of the protocol details from application
developers - however, a rudimentary understanding of the protocol is necessary
to understand the general philosophy and semantics of the parts that remain to
be implemented. You are encouraged to read the `full specification
<https://www.omaspecworks.org/what-is-oma-specworks/iot/lightweight-m2m-lwm2m/>`_,
but this article provides a quick summary of the most important concepts.

.. _clients-and-servers:

Clients and servers
-------------------

A network environment using the LwM2M protocol consists of three types of
entities:

- **LwM2M Clients** are located on end devices. They communicate with server(s),
  allowing them to manage and monitor the devices' resources, which are exposed
  via a standardized `data model`_.

  - A LwM2M Client is uniquely identified independently of its network address
    by an **Endpoint Client Name** - a URN uniquely assigned to a device by its
    manufacturer. OMA recommends the Endpoint Client Name to be in one of the
    following formats:

    - ``urn:uuid:########-####-####-############`` (UUID = Universally Unique
      Identifier)
    - ``urn:dev:ops:{OUI}-{ProductClass}-{SerialNumber}`` (OUI =
      Organizationally Unique Identifier - as in e.g. MAC addresses)
    - ``urn:dev:os:{OUI}-{SerialNumber}``
    - ``urn:imei:###############`` (IMEI = International Mobile Equipment
      Identity)
    - ``urn:esn:########`` (ESN = Electronic Serial Number)
    - ``urn:meid:##############`` (MEID = Mobile Equipment Identifier)
    - ``urn:imei-msisdn:###############-###############`` (MSISDN = Mobile
      Station International Subscriber Directory Number, i.e. a standardized
      phone number)

- **LwM2M Bootstrap Server** is a specific server that may be contacted by the
  Client during its first or every boot-up. Its only purpose is to initialize
  the data model, including connections to regular LwM2M Servers, before first
  contact to such. The Bootstrap Server communicates with the Client using a
  different set of commands, so it cannot be considered a "LwM2M Server" in the
  ordinary sense.

- **LwM2M Servers** maintain connections with the clients and have the ability
  to read from and write to the data model exposed by the clients. Any given
  client may be concurrently connected to more than one LwM2M Server, and each
  of them may have access only to a part of the whole data model.

Anjay is a framework for implementing LwM2M Clients. For this reason, the rest
of this article will be written from the Client perspective.

.. _data-model:

Data model
----------

Each LwM2M Client presents a *data model* - standardized, symbolic
representation of its configuration and state that is accessible for reading
and modifying by LwM2M Servers. It can be thought of as a combination of a
hierarchical configuration file, and a view on statistical information about the
device and its environment.

The LwM2M data model is very strictly organized as a three-level tree. Entities
on each of those levels are identified with numerical identifiers. Those three
levels are:

- **Object** - each Object represent some different concept of data accessible
  via the LwM2M Client. For example, separate Objects are defined for managing
  connections with LwM2M Servers, for managing network connections, for
  accessing data from various types of sensors, etc.

  Each Object is assigned a unique numerical identifier in the range 0-65535,
  inclusive. OMA manages a `registry of known Object IDs
  <https://www.openmobilealliance.org/wp/OMNA/LwM2M/LwM2MRegistry.html>`_. Each
  Object defines a set of Resources whose meanings are common for each Object
  Instance.

- **Object Instance** - some Objects are described as "single-instance" - such
  Objects always have exactly one Instance with identifier 0. Examples of such
  Objects include the Device object which describes the device itself, and the
  Firmware Update object which is used to perform firmware upgrades.

  Other Objects may have multiple Instances; sometimes the number of Instances
  may be variable and the Instances themselves may be creatable via LwM2M.
  Examples of such Objects include the Object that manages connections to LwM2M
  Servers, Object that represents optional software packages installed on the
  device, and Objects representing sensors (whose instances are, however, not
  creatable). Identifiers for each Instance of such Objects may be arbitrarily
  chosen in the range 0-65534, inclusive - note that 65535 is reserved and
  forbidden in this context.

- **Resource** - each Object Instance of a given Object supports the same set
  of Resources, as defined by the Object definition. Within a given Object,
  each Resource ID (which may be in the range 0-65535, inclusive) has a
  well-defined meaning, and represent the same concept. However, some Resources
  may not be present in some Object Instances, and, obviously, their values and
  mapping onto real-world entities may be different.

The numerical identifiers on each of these levels form a path, which is used
as the path portion of CoAP URLs. For example, a path ``/1/2/3`` refers to
Resource ID=3 in Object Instance ID=2 of Object ID=1. Whole Object Instances
(``/1/2``) or event Objects (``/1``) may be referred to using this syntax as
well.

Objects
^^^^^^^

Each Object definition, which may be found in the LwM2M specification, features
the following information:

- **Name** - description of the object; it is not used in the actual on-wire
  protocol.

- **Object ID** - numerical identifier of the Object

- **Instances** - *Single* (always has one Instance with ID=0) or *Multiple*
  (may have arbitrary number of Instances depending on current configuration)

- **Mandatory** - *Mandatory* (must be supported by all LwM2M Client
  implementations) or *Optional* (may not be supported)

- **Object URN**

- Resource definitions

The current set of Mandatory Objects consists of:

- ``/0`` - **LwM2M Security** - contains confidential part of information about
  connections to the LwM2M Servers configured in the Client. From the on-wire
  protocol perspective, it is write-only and accessible only via the
  `Bootstrap Interface`_. Implementation of this object is readily available in
  Anjay's ``security`` module.

- ``/1`` - **LwM2M Server** - contains non-confidential part of information
  about connections to the LwM2M Servers configured in the Client.
  Implementation of this object is readily available in Anjay's ``server``
  module.

- ``/3`` - **Device** - contains basic information about the device, such as
  e.g. serial number.

Additionally, Object ``/2`` (**Access Control**) needs to be supported and
present if the Client supports more than one LwM2M Server connection at once.
Implementation of this object is readily available in Anjay's ``access_control``
module.

.. _lwm2m-resources:

Resources
^^^^^^^^^

Each of the Resource definitions, contained in each Object definition, features
the following information:

- **ID** - numerical identifier of the Object.

- **Name** - short description of the resource; it is not used in the actual
  on-wire protocol.

- **Operations** - one of:

  - **R** - read-only Resource
  - **W** - write-only Resource
  - **RW** - writeable Resource
  - **E** - executable Resource
  - *(empty)* - used only in the LwM2M Security Object; signifies a Resource not
    accessible via the `Device Management and Service Enablement Interface`_

- **Instances** - *Single* or *Multiple*; "Multiple" means that the type of data
  in the resource is actually an "array" - called such in the Anjay API, but
  actually more similar to an associative data structure. It is a list of pairs,
  each of which containing a unique Resource Instance ID (range 0-65535,
  inclusive) and instance value, of the type referred in the Resource
  definition.

- **Mandatory** - *Mandatory* or *Optional*; Mandatory resources need to be
  present in all Instances on all devices. Optional resources may not be present
  in all Instances, and may even be not supported at all on some Clients.

- **Type** - data type of the Resource value (or its instances in case of
  Multiple Resources).

- **Range or Enumeration** - specification of valid values for the Resource,
  within the given data type.

- **Units** - units in which a numerical value is given.

- **Description** - detailed description of the resource.

.. _lwm2m-attributes:

Attributes
^^^^^^^^^^

Each entity in the data model (Resource, Object Instance or Object) can have
various "attributes" attached. There are two types of attributes currently
defined in the LwM2M specification:

- **<PROPERTIES>** Class Attributes are read-only metadata that may be read by
  Servers without accessing the data itself, possibly allowing it to operate
  more effectively. These include:

  - **Dimension** (``dim``) - in case of Multiple Resources, it is the number of
    Resource Instances. Anjay calculates this Attribute automatically, but a
    dedicated callback for calculating it can also be optionally supplied.

  - **Object Version** (``ver``) - provides a way for versioning Object
    definitions. It is currently not supported in Anjay, so all Objects are
    currently locked at version 1.0.

- **<NOTIFICATION>** Class Attributes are writeable by LwM2M Servers and affect
  the way changes in observed resources are notified over the
  `Information Reporting Interface`_.

  By default, *Notify* messages are sent each time there is some change to the
  value of the queried path (which may be a Resource, or all Resources within a
  given Object Instance or Object, if the Observe request was called on such
  higher-level path).

  This behaviour can be modified using the following available attributes:

  - **Minimum Period** (``pmin``) - if set to a non-zero value, notifications
    will never be sent more often than once every ``pmin`` seconds.

  - **Maximum Period** (``pmax``) - if set, notifications will *always* be sent
    at least once every ``pmax`` seconds, even if the value did not change.

  - **Greater Than** (``gt``) and **Less Than** (``lt``) - applicable only to
    numeric Resources - if set, notifications will only be sent when the value
    changes from below to above or from above to below the specified threshold.
    Contrary to what the names of these Attributes might suggest, there is no
    semantic difference between the two - both behave as equivalent
    bi-directional thresholds.

  - **Step** (``st``) - applicable only to numeric Resources - if set,
    notifications will only be sent if the numerical value is different from the
    previously notified value by at least ``st``.

  When several Attributes are specified at the same time, the relations between
  them are as follows:

  - ``pmin`` and ``pmax`` have higher priority - even if the requirements for
    ``gt``, ``lt`` and ``st`` are not met, a notification will always be sent
    at least once every ``pmax`` seconds - and conversely, even when the
    requirements for ``gt``, ``lt`` and ``st`` are met, a notification will
    never be sent more often than once every ``pmin`` seconds.

  - Requirements for just at least one of ``gt``, ``lt`` or ``st`` need to be
    met if they are set at the same time. For example, if the new value differs
    by at least ``st`` from the previously sent one, it does not need to cross
    either of the ``lt``/``gt`` thresholds - the ``st`` condition alone is
    enough to trigger sending notification.

Interfaces
----------

LwM2M currently consists of four interfaces through which the Clients, Servers
and Bootstrap Servers communicate:

- `Bootstrap Interface`_
- `Registration Interface`_
- `Device Management and Service Enablement Interface`_
- `Information Reporting Interface`_

Bootstrap Interface
^^^^^^^^^^^^^^^^^^^

Bootstrap Interface defines the set of commands that the Bootstrap Server may
use to provision the initial configuration onto the client. In this interface,
both the LwM2M Client and the LwM2M Bootstrap Server act as both a CoAP server
and a CoAP client. The messages that may be exchanged between those include:

- **POST /bs?ep={Endpoint Client Name}** request sent from the Client to the
  Bootstrap Server signifies a **Bootstrap Request** command. It informs the
  Bootstrap Server that a new client has appeared on the network and is
  requesting bootstrap information. However, the protocol also allows the
  Bootstrap Server to start issuing Bootstrap commands on its own, without
  receiving a Bootstrap Request message.

- **PUT** requests sent from the Bootstrap Server to the Client are interpreted
  as **Bootstrap Write** commands. These allow creating and writing to Object
  Instances and Resources in order initialize the data model to a state
  appropriate for communication with regular LwM2M Servers.

- **Bootstrap Delete** command, represented as **DELETE** requests from the
  Bootstrap Server to the Client, allows the Bootstrap Server to delete existing
  Object Instances.

- The Bootstrap Server may also send **GET** requests, interpreted as
  **Bootstrap Discover**. It allows the Bootstrap Server to get information
  about the data model supported by and present on the client device. Only a
  list of Object Instances with some additional metadata is accessible. The
  Bootstrap Server *cannot* read Resource values.

- Finally, the Bootstrap Server sends a **Bootstrap Finish** command,
  represented as a **POST /bs** CoAP request send to the Client. Upon receiving
  it, the Client validates the data model, and in case of success, connects to
  regular LwM2M Servers, according to the configured stored within the data
  model.

As you can see, the Bootstrap Interface is mostly write-only. The Bootstrap
Server is not able to do any actual management or monitoring of the Client. It
can only prepare it for communication with regular LwM2M Servers. Nevertheless,
nothing prevents Bootstrap Server and regular Server applications from
coexisting on the same host.

The Bootstrap Server is the only entity that can manage connections to
LwM2M Servers on a Client via the LwM2M protocol itself. For this reason, an
association with a Bootstrap Server may be maintained indefinitely - however,
the protocol also provides an option to permanently disconnect from the
Bootstrap Server after a successful bootstrap.

Bootstrap information may also be provided by means other than the Bootstrap
Server. The protocol also allows the bootstrap information to be pre-provisioned
at the factory, or read from a smart-card. In those cases, an attempt to contact
a Bootstrap Server may not even be made.

.. _lwm2m-registration-interface:

Registration Interface
^^^^^^^^^^^^^^^^^^^^^^

The Registration Interface defines the protocol the Client uses to inform an
LwM2M Server about its presence and availability. In this interface, the LwM2M
Server acts as a CoAP server, and the LwM2M Client is a CoAP client. The
requests that may be sent from the Client to the Server include:

- **Register**, represented as a CoAP **POST /rd?...** request, is initially
  sent by the Client when it goes online. It informs the Server that the Client
  is available for receiving commands on the
  `Device Management and Service Enablement Interface`_ and the
  `Information Reporting Interface`_, and presents it with basic metadata
  describing its data model. It also gives the server the IP address and port
  (or phone number, in case of SMS transport) on which the Client is accessible
  - these are taken directly from the source fields in IP and UDP layer headers.

- **Update**, which is a CoAP **POST** request on an URL previously returned in
  a response to *Register*. **Update** is sent in following situations:

  - periodically - to ensure the Server that the device is still online,

  - whenever any of the information previously given in a Register message
    changes - so that the Server always has up-to-date information about the
    Client's state.

- **De-register** (CoAP **DELETE**) may be sent by the Client if it can
  determine that it is shutting down. It terminates the association between
  the Client and Server. Sending it is, however, not required, as the Server
  will also consider the association terminated if the Client does not report
  with a Register or Update message for a configured period of time.

Device Management and Service Enablement Interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is the main interface on which the actual device management occurs. In this
interface, the LwM2M Server acts as a CoAP client, sending requests to the LwM2M
Client, which acts as a server on the CoAP layer. However, please note that
the IP addresses and port numbers are exactly the same as previously established
via the `Registration Interface`_ - it means that for given two endpoints, the
client/server relationship on the CoAP layer is reversible at any time.

The Device Management and Service Enablement interface defines the following
commands:

- **Discover** (CoAP **GET Accept: application/link-format**) allows the Server
  to get a list of all supported and present Objects, Object Instances and
  Resources, and to read Attributes_ attached to them. Data stored in Resources
  is not returned.

- **Read** (CoAP **GET** other than the above) reads data - either from a single
  Resource, entire Object Instance, or even a whole Object at once.

- **Write** allows the Server to modify the data model. It comes in two
  flavours:

  - **PUT /{Object ID}/{Instance ID}[/{Resource ID}]** request signifies the
    *Replace* method. It can be called on either a single Resource to replace
    its value, or on a whole Object Instance - in that case all existing
    contents of that Instance are erased and replaced with the supplied data.

  - **POST /{Object ID}/{Instance ID}** request means *Partial Update*. It can
    only be called on a whole Object Instance and only replaces the Resources
    present in the request payload, retaining other previously existing data.

  Both methods require the *Content-Format* option to be included in the
  request.

  Anjay attempts to abstract away the difference between the two. All such bulk
  writes are translated to series of writes on single values. However, to
  properly support the *Replace* semantics, an additional virtual operation
  called *Reset* is introduced, called before the series of writes during a
  *Replace* and intended to revert the Object Instance to its initial, empty
  state.

- While most entities in the data model are designed to be read and written,
  a given entity may alternatively be specified as supporting the **Execute**
  operation, represented by a **POST /{Object ID}/{Instance ID}/{Resource ID}**
  request. Execute operation is introduced in the data model wherever a way is
  necessary to instruct the device to perform some non-idempotent operation such
  as a reboot or a firmware upgrade.

- A **PUT** request *without* a *Content-Format* option is interpreted as
  **Write Attributes**. The Attributes are passed as query string elements in
  the target URL. These Attributes mostly alter the way the Client behaves in
  relation to the `Information Reporting Interface`_ and are explained in detail
  in the Attributes_ section.

- A **POST** request targeting one of the root paths in the data model (called
  "Objects", see `Data model`_) represents the **Create** operation. It creates
  a new Object Instance, which gives a way to manage configuration entities that
  might have a variable and configurable number of similar but distinct entries
  - for example, software packages or APN connections.

- Finally, the **Delete** operation (CoAP **DELETE**) is the reverse of Create,
  allowing to remove previously created Object Instances.

Information Reporting Interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This interface can be thought of as an extension to the
`Device Management and Service Enablement Interface`_, allowing the Server to
automatically receive periodic updates about some values in the data model it is
particularly interested in. It is based on the
`OBSERVE extension to CoAP <https://tools.ietf.org/html/rfc7641>`_, applying its
semantics mostly unchanged onto the LwM2M mapping of CoAP concepts.

- A *Read* operation (CoAP **GET**), after adding the **Observe option = 0**,
  becomes **Observe**. Upon receiving such request, in addition to returning the
  current value, the Client will start sending *Notify* messages when
  appropriate.

- **Cancel Observation** command can be issued either by performing a *Read*
  with **Observe option = 1** or by responding to the *Notify* message with a
  **CoAP RESET**.

- **Notify** is an **asynchronous CoAP response** as described in
  `RFC 7641 <https://tools.ietf.org/html/rfc7641>`_. It is essentially a
  repeated reply to a *Read*, sent whenever the observed value changes, and/or
  periodically, according to relevant Attributes_.

  It may be sent as a Non-confirmable or as a Confirmable message at discretion
  of the Client. Anjay currently sends almost all notifications as
  Non-confirmable messages; Confirmable notifications are sent once every 24
  hours, to comply with
  `RFC 7641 section 4.5 <https://tools.ietf.org/html/rfc7641#page-18>`_.

Queue mode
----------

The *Register* and *Update* commands include a "Binding Mode" Parameter, which
may be one of the following:

- ``U`` - UDP connection in standard mode
- ``UQ`` - UDP connection in queue mode
- ``S`` - SMS connection in standard mode
- ``SQ`` - SMS connection in queue mode
- ``US`` - both UDP and SMS connections active, both in standard mode
- ``UQS`` - both UDP and SMS connections active; UDP in queue mode, SMS in
  standard mode

The "queue mode" mentioned here is a special mode of operation in which the
client device is not required to actively listen for incoming packets. The
client is only required to listen for such packets for a limited period of time
after each exchange of messages with the Server - typically after the *Update*
command.

The specification recommends to use CoAP's ``MAX_TRANSMIT_WAIT`` value (93
seconds by default) as that aforementioned limited period of time, and this
recommendation is respected in Anjay.

Anjay automatically handles the queue mode by hiding connections which are not
required to actively listen from the library user. In particular, if the
``anjay_get_sockets()`` function returns an empty list, it likely means that all
active connections are in queue mode and the listening period has passed. In
that case, it is safe to passively sleep for the period returned by
``anjay_sched_time_to_next()`` (or one of its convenience wrappers).
