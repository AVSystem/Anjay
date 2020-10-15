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

LwM2M testing shell
-------------------

For the purpose of early testing of Anjay-based clients, we provide a simple CLI implementation of
LwM2M that supports the UDP and TCP (although the TCP support is in a very early stage, and it may
not be as stable as UDP) protocols. It is written in Python using `powercmd` library.
You can find it in commercial version of Anjay in `bootstrap/framework/nsh_lwm2m/` directory.

Running the server
~~~~~~~~~~~~~~~~~~

You can start the server (from the main Anjay directory) by running `./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py`
with the following optional arguments:

--help, -h            Show help.
--ipv6, -6            Use IPv6 by default.
--listen PORT, -l PORT
                      Immediately starts listening on specified CoAP port. Default UDP, can be TCP if --tcp is passed. If `PORT` is not specified, default one is used (5683 for CoAP, 5684 for CoAP/(D)TLS)
--tcp, -t
                      Listen on TCP port
--psk-identity IDENTITY, -i IDENTITY
                      PSK identity to use for DTLS connection (literal string).
--psk-key KEY, -k KEY
                      PSK key to use for DTLS connection (literal string).
--debug               Enable mbed TLS debug output.

Supported commands
~~~~~~~~~~~~~~~~~~

In this section we present the commands supported by the shell, with a bunch of examples.
The initial setup for most of the provided examples can be obtained by two commands, both runned from the main Anjay directory,
first for setting up the server:

.. code-block:: bash

   ./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py -l 9000

and the second for setting up the client:

.. code-block:: bash

   ./output/bin/demo -e my_endpoint -u coap://127.0.0.1:9000


Handling messages
^^^^^^^^^^^^^^^^^

The most important group of commands are those for sending messages. Most of them just send the corresponding LwM2M message:

bootstrap_finish ``MSG_ID`` ``TOKEN`` ``OPTIONS`` ``CONTENT``
   ..
changed ``MSG_ID`` ``TOKEN`` ``LOCATION`` ``OPTIONS`` ``CONTENT``
   ..
coap_get ``PATH`` ``ACCEPT`` ``MSG_ID`` ``TOKEN`` ``LOCATION``
   ..
content  ``MSG_ID`` ``TOKEN`` ``CONTENT`` ``FORMAT`` ``TYPE`` ``OPTIONS``
   ..
continue ``MSG_ID`` ``TOKEN`` ``TYPE`` ``OPTIONS``
   ..
create ``PATH`` ``CONTENT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS`` ``FORMAT``
   ..
created ``MSG_ID`` ``TOKEN`` ``LOCATION`` ``OPTIONS``
   ..
delete ``PATH`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
deleted ``MSG_ID`` ``TOKEN`` ``LOCATION`` ``OPTIONS``
   ..
deregister ``PATH`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
discover ``PATH`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
empty
   ..
error_response ``CODE`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
est_coaps_simple_enroll ``MSG_ID`` ``TOKEN`` ``URI_PATH`` ``URI_QUERY`` ``OPTIONS`` ``CONTENT``
   ..
execute ``PATH`` ``CONTENT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
notify ``TOKEN`` ``CONTENT`` ``FORMAT`` ``CONFIRMABLE`` ``OPTIONS``
   ..
observe ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
observe_composite ``PATHS`` ``OBSERVE`` ``ACCEPT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
read ``PATH`` ``ACCEPT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
read_composite ``PATHS`` ``ACCEPT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
request_bootstrap ``ENDPOINT_NAME`` ``PREFERRED_CONTENT_FORMAT`` ``MSG_ID`` ``TOKEN`` ``URI_PATH`` ``URI_QUERY`` ``OPTIONS`` ``CONTENT``
   ..
reset ``MSG_ID``
   ..
send ``PATH`` ``MSG_ID`` ``TOKEN`` ``FORMAT`` ``OPTIONS`` ``CONTENT``
   ..
write ``PATH`` ``CONTENT`` ``FORMAT`` ``UPDATE`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
write_attributes ``PATH`` ``LT`` ``GT`` ``ST`` ``PMIN`` ``PMAX`` ``EPMIN`` ``EPMAX`` ``QUERY`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..
write_composite ``CONTENT`` ``FORMAT`` ``MSG_ID`` ``TOKEN`` ``OPTIONS``
   ..

Because sometimes it might be necessary to use Write message with a large block of data (e.g. when performing a firmware update)
it may be handy to load the data from a file instead of writing it by hand.
Nsh supports an additional command for this case:

write_file ``FNAME`` ``PATH`` ``FORMAT`` ``CHUNKSIZE`` ``TIMEOUT_S``
   Opens file ``FNAME`` and attempts to push it using BLOCK1 to the Client.

It is also possible to send a custom CoAP message/UDP datagram using Nsh:

coap ``TYPE`` ``CODE`` ``MSG_ID`` ``TOKEN`` ``OPTIONS`` ``CONTENT`` ``RESPOND``
   Send a custom CoAP message.
udp ``CONTENT``
   Send a custom UDP datagram.

And finally, there is a command waiting for a message sent by the client:

recv ``TIMEOUT_S``
   Waits for a next incoming message. If ``TIMEOUT_S`` is specified, the
   command will not wait longer than ``TIMEOUT_S`` if no messages are received.

Nsh provide also one more feature for handling messages.
When the user enters an empty line while the flags ``AUTO_UPDATE``, ``AUTO_REREGISTER`` or ``AUTO_ACK``
(see **set** command description)
are set, the server tries to handle the corresponding messages from the client,
responding them in a proper way. For example, when client sends notify
the result of entering an empty line on Nsh side should be:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $
   <- Register /rd?lwm2m=1.1&ep=my_endpoint&lt=86400: </1/1>,</2>,</3/0>,</4/0>,<...
   -> Created /rd/demo
   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $

.. note::

   Usually there is no need for passing all of the command arguments. To see which are optional
   you can use **help** for the considred command. In the output they are printed with ``?`` signs.

Working with payloads
^^^^^^^^^^^^^^^^^^^^^

Introduction
""""""""""""

When a binary payload contains a non-printable character, it is
impossible to encode it as a plain text. To overcome this inconvenience the shell introduces a special
type: ``EscapedBytes``, in which you can hex-encode some of the bytes (in many cases it might be quite handy to
hex-encode just all of them): after ``\x`` the following two characters are interpreted as hex digits encoding
one byte. Examples of the binary payloads encoded in such way can be found below, while discussing subshells.

Preparing or reading data in such format may be quite painful so Nsh has tools to make it more comfortable.
To build TLV or CBOR payloads (which are binary formats), nsh exposes subshells.
Each of them has its own set of commands, however, some of them are common.
**help**, **get_error** and **exit** behave in a similar way to those known from the main shell.
Other commands common for the subshells are:

serialize
   Displays the prepared strucure as an encoded hex-escaped string (ready to use as EscapedBytes).
show
   Displays current element structure in a human-readable form.

CBOR subshell
"""""""""""""

This subshell is entered by **cbor** command. The only extra command supported is:

add_resource ``BASENAME`` ``NAME`` ``TYPE`` ``VALUE``
   Adds the next entry to the existing CBOR data. ``BASENAME`` argument is optional and it can contain the parent path. In ``NAME``
   a path to some value-containing Resource/Resource Instance is kept.

TLV subshell
""""""""""""

TLV subshell is entered by **tlv** command. It supports a few commands more:

add_instance ``ID``
   Creates an object instance with a given ``ID``. It must be created as a top-level element.
add_multiple_resource ``ID``
   Creates a Multiple Resource under the currently selected Object Instance (as a top-level element, if none is selected).
add_resource ``ID`` ``VALUE`` ``TYPE``
   Creates a Resource with a given ``ID`` under the currently selected Object Instance. If there is none, it is created as a top-level element.
add_resource_instance ``ID`` ``VALUE`` ``TYPE``
   Creates a Resource Instance of the currently selected Multiple Resource.
deserialize ``DATA``
   Loads a TLV-encoded element structure for further processing. It is helpful, when we recieve data from *read* request from the client.
make_multires ``(RIID,VALUE),...``
   Builds Multiple Resource Instances from the list of pairs of ``RIID`` and ``VALUE`` (of type ``EscapedBytes``).
   The pairs need to be comma separated and no spaces are allowed.

   For example ``(1,\x04),(5,\x02)`` represents two object instances, first with ID 1 and value 4 and second with ID 5 and value 2.
remove ``PATH``
   Removes an element to which the path points. The path consists of 1 - 3 integers, separated by ``/`` character.
select ``PATH``
   Selects an Object Instance or Multiple Resource that further add_* calls will add elements into.


Using subshells example
"""""""""""""""""""""""

Let's suppose that we would like to encode some simple data as both CBOR ans TLV, let its structure be:

.. code-block:: text

   /0 (Instance)
     -> /0 (Multiple Resource)
       -> 0 = 2 (Resource Instance)
       -> 1 = 5 (Resource Instance)
   /1 (Instance)
     -> /1 = 11 (Resource)
     -> /3 = 1 (Resource)

To encode it as TLV, we need to enter the following commands:

.. code-block:: text

   add_instance 0
   add_multiple_resource 0
   make_multires (0,\x02),(1,\x05)
   add_instance 1
   add_resource 1 type=int 11
   add_resource 3 type=int 1

After running these commands, the TLV data are ready, and you can see the result in human-readable form using **show** command:

.. code-block:: text

   [Lwm2mCmd/TLV] port: 9000, client: 127.0.0.1:47748 $ show
   * exact: show
     path    value
   ---------------
     0       instance (1 resources)
     0/0       multiple resource (2 instances)
     0/0/0       resource instance = b'\x02' (int: 2)
     0/0/1       resource instance = b'\x05' (int: 5)
   * 1       instance (2 resources)
     1/1       resource = b'\x0b' (int: 11)
     1/3       resource = b'\x01' (int: 1)


and when we escape the subshell with **exit** command, we will recieve
the created data in form of `EscapedBytes`:

.. code-block:: text

   [Lwm2mCmd/TLV] port: 9000, client: 127.0.0.1:47748 $ exit
   * exact: exit
   exiting
   \x08\x00\x08\x86\x00\x41\x00\x02\x41\x01\x05\x06\x01\xc1\x01\x0b\xc1\x03\x01

In CBOR the number of commands will be smaller, as we run them only for leaves:

.. code-block:: text

   add_resource 0/0/0 int 2
   add_resource 0/0/1 int 5
   add_resource 1/1 int 11
   add_resource 1/3 int 1

which gives us the following CBOR data:

.. code-block:: text

   [Lwm2mCmd/CBOR] port: 9000, client: 127.0.0.1:47748 $ show
   * exact: show
   CBOR (4 elements):

     {<SenmlLabel.NAME: 0>: '0/0/0', <SenmlLabel.VALUE: 2>: 2}
     {<SenmlLabel.NAME: 0>: '0/0/1', <SenmlLabel.VALUE: 2>: 5}
     {<SenmlLabel.NAME: 0>: '1/1', <SenmlLabel.VALUE: 2>: 11}
     {<SenmlLabel.NAME: 0>: '1/3', <SenmlLabel.VALUE: 2>: 1}

and, in the same way as in the case of the TLV subshell, we escape
the shell and recieve the encoded data:

.. code-block:: text

   [Lwm2mCmd/CBOR] port: 9000, client: 127.0.0.1:47748 $ exit
   * exact: exit
   exiting
   \x84\xa2\x00\x65\x30\x2f\x30\x2f\x30\x02\x02\xa2\x00\x65\x30\x2f\x30\x2f\x31\x02\x05\xa2\x00\x63\x31\x2f\x31\x02\x0b\xa2\x00\x63\x31\x2f\x33\x02\x01

Decoding messages
^^^^^^^^^^^^^^^^^

Nsh supports two commands which are connected to both previously discussed topics - tools for decoding CoAP/LwM2M messages:

coap_decode ``DATA``
   Decodes a CoAP message and displays it in a human-readable form.
lwm2m_decode ``DATA``
   Decodes a LwM2M message and displays it in a human-readable form.

For example, we can decode an empty coap message (with *EscapedBytes* representation ``\x60\x00\x13\x38``):

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ coap_decode \x60\x00\x13\x38
   * exact: coap_decode
   version: 1
   type: ACKNOWLEDGEMENT
   code: 0.00 (EMPTY)
   msg_id: 4920
   token:  (length: 0)
   options:

   content: 0 bytes

Inspecting previous messages
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Nsh supports also a bunch of tools for inspecting the results of the previous commands.

Message history
"""""""""""""""

The first such tool is the *message history* which can be handled using two commands:

details ``N``
   Displays details of a ``N``-th last message, or the last message, if ``N`` is not given.
reset_history
   Clears command history.

To see how they work, let's send a few messages, e.g.:

.. code-block:: text

   read /1/1/3/1
   empty
   reset

Now, we can check N-th message, sent or recieved, by running ``details N``
(important note: the last message has N=1). For example, in such case running ``details 4`` would return:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ details 4
   * exact: details

   *** Send ***
   Read /1/1/3/1

   version: 1
   type: CONFIRMABLE
   code: 0.01 (REQ_GET)
   msg_id: 4920
   token: NbwK\x18W\xc7\xcb (length: 8)
   options:
      option 11 (URI_PATH), content (1 bytes): 1
      option 11 (URI_PATH), content (1 bytes): 1
      option 11 (URI_PATH), content (1 bytes): 3
      option 11 (URI_PATH), content (1 bytes): 1
   content: 0 bytes

   ascii-ish:

We can use also run this command without parameters, to see the last message:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ details
   * exact: details

   *** Send ***
   Reset, msg_id = 4922

   version: 1
   type: RESET
   code: 0.00 (EMPTY)
   msg_id: 4922
   token:  (length: 0)
   options:

   content: 0 bytes

   ascii-ish:

After running the **reset_history** command, the history will be cleared and
**details** (with any parameter) runned after that, returns only a warning ``message not found``.

Payload buffer
""""""""""""""

Another important tool is **payload buffer**.
It stores the contents of the messages recieved by the server and
can be accessed with a set of functions **payload_buffer_\***:

payload_buffer_clear
   Clears payload buffer.
payload_buffer_show
   Shows the payload buffer content.
payload_buffer_show_hex
   Shows the payload buffer content presented as hex.
payload_buffer_show_tlv
   Shows the payload buffer content presented as tlv.

Let's see an example. After reading an object instance (with some human readable format, e.g. *JSON*):

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ read /1/1 APPLICATION_LWM2M_JSON
   * exact: read
   -> Read /1/1: accept APPLICATION_LWM2M_JSON
   <- Content (11543 (APPLICATION_LWM2M_JSON); 193 bytes)

the content of the message can be printed data using **payload_buffer_show**. The result should be similar to:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ payload_buffer_show
   * exact: payload_buffer_show
   b'{"bn":"/1/1","e":[{"n":"/0","v":1},{"n":"/1","v":86400},{"n":"/6","bv":true},{"n":"/7","sv":"U"},{"n":"/17","v":1},
   {"n":"/18","v":0},{"n":"/19","v":1},{"n":"/20","v":0},{"n":"/23","bv":false}]}'

Sometimes it is quite useful to represent the data as hex-encoded bytes, what can be obtained with **payload_buffer_show_hex**, which for the considered JSON data
looks like:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ payload_buffer_show_hex
   * exact: payload_buffer_show_hex
   \x7b\x22\x62\x6e\x22\x3a\x22\x2f\x31\x2f\x31\x22\x2c\x22\x65\x22\x3a\x5b\x7b\x22\x6e\x22\x3a\x22\x2f\x30\x22\x2c\x22
   \x76\x22\x3a\x31\x7d\x2c\x7b\x22\x6e\x22\x3a\x22\x2f\x31\x22\x2c\x22\x76\x22\x3a\x38\x36\x34\x30\x30\x7d\x2c\x7b\x22
   \x6e\x22\x3a\x22\x2f\x36\x22\x2c\x22\x62\x76\x22\x3a\x74\x72\x75\x65\x7d\x2c\x7b\x22\x6e\x22\x3a\x22\x2f\x37\x22\x2c
   \x22\x73\x76\x22\x3a\x22\x55\x22\x7d\x2c\x7b\x22\x6e\x22\x3a\x22\x2f\x31\x37\x22\x2c\x22\x76\x22\x3a\x31\x7d\x2c\x7b
   \x22\x6e\x22\x3a\x22\x2f\x31\x38\x22\x2c\x22\x76\x22\x3a\x30\x7d\x2c\x7b\x22\x6e\x22\x3a\x22\x2f\x31\x39\x22\x2c\x22
   \x76\x22\x3a\x31\x7d\x2c\x7b\x22\x6e\x22\x3a\x22\x2f\x32\x30\x22\x2c\x22\x76\x22\x3a\x30\x7d\x2c\x7b\x22\x6e\x22\x3a
   \x22\x2f\x32\x33\x22\x2c\x22\x62\x76\x22\x3a\x66\x61\x6c\x73\x65\x7d\x5d\x7d

To use the function **payload_buffer_show_tlv** we need some data in TLV format, so with the current payload it prints only an error:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ payload_buffer_show_tlv
   * exact: payload_buffer_show_tlv
   attempted to take 7217722 bytes, but only 187 available (try "get_error" for details)

Moreover, after reading the object instance with ``read /1/1 APPLICATION_LWM2M_TLV``, the result will be the same.
The reason of such behaviour is that there is some data in payload which is not in TLV encoding.
In such case **payload_buffer_clear** is needed before:

.. code-block:: text

   payload_buffer_clear
   read /1/1 APPLICATION_LWM2M_TLV
   payload_buffer_show_tlv

And finally some nice, human-readable TLV representation is printed:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ payload_buffer_show_tlv
   * exact: payload_buffer_show_tlv
   TLV (9 elements):

     resource 0 = b'\x01' (int: 1)
     resource 1 = b'\x00\x01Q\x80' (int: 86400, float: 0.000000)
     resource 6 = b'\x01' (int: 1)
     resource 7 = b'U' (int: 85)
     resource 17 = b'\x01' (int: 1)
     resource 18 = b'\x00' (int: 0)
     resource 19 = b'\x01' (int: 1)
     resource 20 = b'\x00' (int: 0)
     resource 23 = b'\x00' (int: 0)

Checking errors
"""""""""""""""

When something was wrong with your last command Nsh will return an error.
It might be helpful to get some more details and for this purpose you can
use **get_error** command. To see how it works, let's try the following **read**:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ read 0/3
   * exact: read
   could not send Lwm2mRead (not a valid CoAP path: 0/3) (try "get_error" for details)


Some error was returned, so
**get_error** command can be used to see some details. A similar trace should be printed:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:47748 $ get_error
   * exact: get_error
   Traceback (most recent call last):
   File "./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py", line 862, in send_msg
      self._send(cls(*args, **kwargs))
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/lwm2m/messages.py", line 636, in __init__
      path = Lwm2mNonemptyPath(path)
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/lwm2m/path.py", line 62, in __init__
      super().__init__(text)
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/lwm2m/path.py", line 32, in __init__
      super().__init__(text)
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/lwm2m/path.py", line 13, in __init__
      raise ValueError('not a valid CoAP path: %s' % (text,))
   ValueError: not a valid CoAP path: 0/3

   During handling of the above exception, another exception occurred:

   Traceback (most recent call last):
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/powercmd/powercmd/cmd.py", line 173, in default
      return invoker.invoke(self, cmdline=CommandLine(cmdline))
   File "/home/mziobro/anjay/bootstrap/framework/nsh-lwm2m/powercmd/powercmd/command_invoker.py", line 208, in invoke
      return cmd.handler(*args, **typed_args)
   File "./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py", line 864, in send_msg
      raise e.__class__('could not send %s (%s)' % (cls.__name__, e))
   ValueError: could not send Lwm2mRead (not a valid CoAP path: 0/3)


As we can see, the error was raised in line 13 of ``path.py``:

.. code-block:: python

   def __init__(self, text):
      if not text.startswith('/'):
         raise ValueError('not a valid CoAP path: %s' % (text,))

Now the issue with the path is clear - it is not started with ``/`` character.

Dealing with connections
^^^^^^^^^^^^^^^^^^^^^^^^

To this point we always used the same setting of the client and the server, with the server port
given as a command line parameter. This approach is sufficient for most of cases, but Nsh supports
three commands for modyfing the connection in runtime:

connect ``HOST`` ``PORT``
   Connects the socket to given ``HOST:PORT``. Future packets will be sent to this address.
listen ``PORT`` ``PSK_IDENTITY`` ``PSK_KEY`` ``CA_PATH`` ``CA_FILE`` ``CRT_FILE`` ``KEY_FILE`` ``IPV6`` ``DEBUG`` ``CONNECTION_ID``
   Starts listening on given ``PORT``. If any of ``PSK_IDENTITY``, ``PSK_KEY``, ``CA_PATH``, ``CA_FILE``, ``CRT_FILE`` or ``KEY_FILE`` are specified, sets up a DTLS server, otherwise - raw CoAP server.
unconnect
   "Unconnects" the socket from an already accepted client. The idea is that then the server will be able to receive packets from different (host, port), which may be useful for testing purposes.


Testing
^^^^^^^

We can use Nsh for running list of commands from a file, working as a kind of a primitive test case.
There are two commands which can be especially helpful in such situation:

expect ``MSG_CODE``
   Makes the shell compare next received packet against the one configured
   via this command and print a message if a mismatch is detected.

   ``MSG_CODE`` can be:

   - a string with Python code that evalutes to a correct message,

   - None, if no messages are expected,

   - ANY to disable checking (default).

   Note: after receiving each message the "expected" value is set to ANY.
sleep ``TIMEOUT_S``
   Blocks for ``TIMEOUT_S`` seconds. Might be helpful when we want to be sure that the client have enough
   time to make some action.

Different kinds of servers
^^^^^^^^^^^^^^^^^^^^^^^^^^

Besides a casual LwM2M server, Nsh can also serve in two different ways:

 1. as a bootstrap LwM2M server,
 2. for serving files over CoAP.

They are implemented with the following commands (respectively):

bootstrap ``URI`` ``SECURITY_MODE`` ``PSK_IDENTITY`` ``PSK_KEY`` ``CLIENT_CERT_PATH`` ``CLIENT_PRIVATE_KEY_PATH`` ``SERVER_CERT_PATH`` ``SSID`` ``IS_BOOTSTRAP`` ``LIFETIME`` ``NOTIFICATION_STORING`` ``BINDING`` ``IID`` ``FINISH`` ``TLS_CIPHERSUITES``
   Sets up a Security and Server instances for an LwM2M server.

   In case of PreSharedKey security mode, ``PSK_IDENTITY`` and ``PSK_KEY``
   are literal plain text sequences to be used as DTLS identity and secret key.

   In case of Certificate security mode, ``CLIENT_CERT_PATH`` and
   ``SERVER_CERT_PATH`` shall be paths to binary DER-encoded X.509
   certificates, and ``CLIENT_PRIVATE_KEY_PATH`` to binary DER-encoded
   PKCS#8 file, which MUST NOT be password-protected.

   If ``IS_BOOTSTRAP`` is True, only the Security object instance is
   configured. ``LIFETIME``, ``NOTIFICATION_STORING`` and ``BINDING`` are ignored
   in such case. ``SSID`` is still set for the Security instance.

   Both Security and Server object instances are created with given ``IID``.

   If ``FINISH`` is set to True, a *Bootstap Finish* message will be sent
   after setting up Security/Server instances.
file_server ``ROOT_DIRECTORY`` ``PORT`` ``PSK_IDENTITY`` ``PSK_KEY`` ``CA_PATH`` ``CA_FILE`` ``CRT_FILE`` ``KEY_FILE`` ``IPV6`` ``DEBUG``
   Serves files from ``ROOT_DIRECTORY`` over CoAP(s).

As they are the most complex commands, we provide examples for both of them:

Bootstrapping
"""""""""""""

To show how we can use Nsh for bootstrapping, we set up the bootstrap server:

.. code-block:: text

   ./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py -l 9000

and the second one (in some other terminal), this time on a different port and using some id and password
(for the sake of simplicity the id=`user`and password=`password`):

.. code-block:: text

   ./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py -l 9500 --psk-identity user --psk-key password

Then we run the client (important note: ``--bootstrap`` option is necessary):

.. code-block:: text

   ./output/bin/demo -e my_endpoint -u coap://127.0.0.1:9000 --bootstrap

At this point the client is connected to the first server and we need to provide it information sufficient
for connecting the second server:

.. code-block:: text

   [Lwm2mCmd] port: 9000, client: 127.0.0.1:41266 $ bootstrap finish=True ssid=1 uri=coaps://127.0.0.1:9500 security_m
   ode=PreSharedKey psk_identity=user psk_key=password
   * exact: bootstrap
   -> Write /0: APPLICATION_LWM2M_TLV, 58 bytes
   <- Changed (no location path)
   -> Write /1: APPLICATION_LWM2M_TLV, 18 bytes
   <- Changed (no location path)
   -> Bootstrap Finish /bs:
   <- Changed (no location path)

Now the client is connected to the second server. As we can see in the bootstrap server log, it sent 3 messages to the client,
two Writes to set the Server and Security objects and Bootstrap Finish in the end.

Serving files over CoAP
"""""""""""""""""""""""

To see how we can use Nsh for serving files, first start it without arguments:

.. code-block:: text

   ./bootstrap/framework/nsh-lwm2m/nsh_lwm2m.py

and then start serving files from Anjay directory:

.. code-block:: text

   [Lwm2mCmd] $ file_server . 9000
   * exact: file_server
   Serving directory /home/mziobro/anjay on port 9000...
   Press CTRL-C to stop

Currently we do not have to connect, so we can run the client with any URI starting with `coap://`

.. code-block:: text

   ./output/bin/demo -e my_endpoint -u coap://anything

Because the URI is invalid, we will recieve a few errors, but the client will run.
Now, we use **download** command on the client side. Assuming that we are in the same (i.e. Anjay)
directory, it will just copy one of the files (in this case, we download Makefile to Makefile_copy):

.. code-block:: text

   download coap://127.0.0.1:9000/Makefile Makefile_copy

Miscellaneous
^^^^^^^^^^^^^

There are a few commands, rather simple, which does not fit in any previous category:

exit
   Terminates the command loop. Equivalent to ``Ctrl+D``.
help
   Displays a description of given command or lists all available commands.
set ``AUTO_UPDATE`` ``AUTO_REREGISTER`` ``AUTO_ACK``
   Sets in which situation server sends a message to a client automatically:

   - ``AUTO_UPDATE`` - when LwM2M Update is received from the client,

   - ``AUTO_REREGISTER`` - when LwM2M Register is received from the client,

   - ``AUTO_ACK`` - after any confirmable message from the client.

   If some of the options are absent, their state remains unchanged.

