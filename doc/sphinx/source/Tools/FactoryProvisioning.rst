..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Factory Provisioning Tool
=========================

General overview
^^^^^^^^^^^^^^^^

Anjay comes with a simple Python script that makes factory device
provisioning easier. The script supports:

* Generation of SenML CBOR encoded packet with basic object information read by Anjay.
* Creation of self-signed certificates.
* Loading configuration to device (currently supports Nordic boards).
* Automatic device onboarding in the Coiote server.

Provisioning tool
^^^^^^^^^^^^^^^^^

The provisioning tool is a small script that the user will interact with
during the factory provisioning process. It uses the Factory Provisioning library
that we will talk about in the next section.

.. note::
   The script can be found in ``tools/provisioning-tool/ptool.py``.

The script takes many different parameters as arguments to allow some customisation.
Let's take a closer look:

* `-c`, `--endpoint_cfg` - path to the configuration file with the object information
  to be loaded to the device. The file is in the format of a Python dictionary that
  is evaluated by the library and represents LwM2M objects to be loaded to the
  device. In addition to the standard Python data types (``int``, ``str``, ``bool``,
  ``bytes``) to represent values of resources in the object, the user can use ``Objlink``
  class to represent objlnk resource type. The constructor for this class is as
  following:

  .. highlight:: python
  .. snippet-source:: tests/integration/framework/test_utils.py

      class Objlink:
          def __init__(self, ObjID, ObjInstID):
              self.ObjID = ObjID
              self.ObjInstID = ObjInstID

  So for instance ``Objlink(66, 0)`` would represent a object link ``66:0``.

  .. note::
     Sample configuration can be found in ``tools/provisioning-tool/configs/endpoint_cfg``

  .. warning::

      The provisioning tool is **NOT** intended to be safe to use with
      arbitrary input data. It is only intended for convenience when working with
      files created locally by trusted parties.

      **The input text file ("endpoint_cfg") is evaluated as Python code**, and as
      such, running the code from provisioning tool with untrusted input may lead
      to arbitrary operations being performed on the computer.

* `-e`, `--URN` - This is the name of the device that will be used during registration
  to the Coiote Server. The name should be unique for the instance of Coiote Server
  we will register the device to.
* `-s`, `--server` - This is a JSON file with server information needed for
  registration process. Those include:

    * `url` - url of the Coiote Server, if missing a default value ``https://eu.iot.avsystem.cloud``
      is used.
    * `port` - port number communication with the REST API, if missing a default value
      ``8087`` is used. Please note that this is not the port number used by the endpoint
      device for communication with the Coiote server and rather a port number used by the
      REST API.
    * `domain` - name of the domain under which to register the device. There is no
      default value and this needs to be provided by the user if a registration process
      is performed.

  .. note::
     Sample configuration can be found in ``tools/provisioning-tool/configs/lwm2m_server.json``

* `-t`, `--token` - Access token for REST authorization to the Coiote server.
  The generation of this token is explained in the Coiote documentation. In Coiote click
  on the question mark in the top right corner, then **Documentation -> User**. The
  description can be found in **Rest API -> REST API authentication** section.
* `-C`, `--cert` - Path to the JSON file containing information for the generation of
  a self signed certificate. The provisioning tool supports those JSON entries:

  +------------------------+---------+---------------+----------------------------------------+
  | Field name             | Type    | Default Value | Description                            |
  +========================+=========+===============+========================================+
  | countryName            | String  | N/A           | Holds a 2-character ISO format country |
  |                        |         |               | code. Represents attribute C in        |
  |                        |         |               | certificate subject.                   |
  +------------------------+---------+---------------+----------------------------------------+
  | stateOrProvinceName    | String  | N/A           | Represents attribute ST in certificate |
  |                        |         |               | subject.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | localityName           | String  | N/A           | Represents attribute L in certificate  |
  |                        |         |               | subject.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | organizationName       | String  | N/A           | Represents attribute O in certificate  |
  |                        |         |               | subject.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | organizationUnitName   | String  | N/A           | Represents attribute OU in certificate |
  |                        |         |               | subject.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | emailAddress           | String  | N/A           | Holds email address.                   |
  +------------------------+---------+---------------+----------------------------------------+
  | commonName             | String  | <endpoint     | Represents attribute CN in certificate |
  |                        |         | name>         | subject.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | serialNumber           | Integer | N/A           | Holds serial number attribute in the   |
  |                        |         |               | certificate subject.                   |
  +------------------------+---------+---------------+----------------------------------------+
  | validityOffsetInSeconds| Integer | 220752000     | Represents validity of certificate in  |
  |                        |         |               | seconds.                               |
  +------------------------+---------+---------------+----------------------------------------+
  | ellipticCurve          | String  | "secp256r1"   | Elliptic curve on which to base the    |
  |                        |         |               | key generated during certificate       |
  |                        |         |               | creation.                              |
  +------------------------+---------+---------------+----------------------------------------+
  | RSAKeyLen              | Integer | N/A           | Represents length of the RSA key used  |
  |                        |         |               | during certificate creation.           |
  |                        |         |               |                                        |
  |                        |         |               | Cannot be specified together with      |
  |                        |         |               | ``ellipticCurve``. EC-based keys are   |
  |                        |         |               | used by default.                       |
  +------------------------+---------+---------------+----------------------------------------+
  | digest                 | String  | "sha256"      | Represents a digest algorithm used     |
  |                        |         |               | during certificate signing.            |
  +------------------------+---------+---------------+----------------------------------------+

  .. note::
     Sample configuration can be found in ``tools/provisioning-tool/configs/cert_info.json``

* `-k`, `--pkey` - Path to the endpoint private key in DER format, ignored if CERT
  parameter is set.
* `-r`, `--pcert` - Path to the endpoint private cert in DER format, ignored if CERT
  parameter is set.
* `-p`, `--scert` - Path to the server public cert in DER format.

.. note::
    The server public certificate in DER format can be acquired using openssl client:
    ``echo -n | openssl s_client -connect SERVER:PORT | openssl x509 -outform der > CERTIFICATE.der``
    or converted from PEM format using:
    ``openssl x509 -outform der -in CERTIFICATE.pem -out CERTIFICATE.der``.

Factory Provisioning library
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To better understand the provisioning process we will look into the implementation
of the Factory Provisioning library.

.. note::
   The Python library for factory provisioning can be found in
   ``tools/provisioning-tool/factory_prov``.

The main class of the library that the user will interact with is the ``FactoryProvisioning``
class. The constructor for this class takes a few different arguments:

.. highlight:: python
.. snippet-source:: tools/provisioning-tool/factory_prov/factory_prov.py

    class FactoryProvisioning:
       def __init__(self,
                    endpoint_cfg,
                    endpoint_name,
                    server_info,
                    token,
                    cert_info):


The ``endpoint_cfg`` is the path to the file with the device configuration. Corresponds
to the argument of the same name from the provisioning tool.

The next parameter is ``endpoint_name``. This is the unique name of the device used
during registration. Corresponds to the `URN` argument from the provisioning tool.

The ``server_info`` is the path to the file with Coiote server information. Corresponds
to `server` argument from the provisioning tool.

The ``token`` parameter is a token used to authenticate to the REST API. Corresponds
to the argument fo the same name from the provisioning tool.

The ``cert_info`` parameter can be used to pass the path to a file containing information
used during generation of a self signed certificate. This parameter corresponds to
`cert` argument from the provisioning tool.

.. note::
   Parameters ``server_info``, ``token`` and ``endpoint_name``  can be set to ``None`` if
   automatic registration to the Coiote server won't be done. Also `cert_info`
   parameter can be ``None`` if the user won't create a self signed certificate using
   the factory provisioning library or security mode used will be different then
   Certificate.

The user can extract the information about used Security Mode set in ``endpoint_cfg``
using a class method ``get_sec_mode()``. This returns a string containing one
of three values: "psk", "cert", "nosec".

If Certificate is used as a Security Mode in the Security object definition,
then before calling ``provision_device()``:

- user should call ``set_server_cert()`` function to pass a path to a DER
  formatted file containing server's certificate,
- ``generate_self_signed_cert()`` should be called or pre-generated
  certificates should be supplied by calling ``set_endpoint_cert_and_key()``
  with a path to device's private key and public certificate.

To perform the factory provisioning of the device the user should call ``provision_device()``
from the ``FactoryProvisioning`` class. This function will generate a configuration
file in the format os SenML CBOR (the file will be called "SenMLCBOR" and writen to disk). This
configuration will be uploaded to the device together with the certificates (either
self signed client certificates or the cerificate pointed by ``set_endpoint_cert_and_key()``
and also the server certificate set using ``set_server_cert()``.

The ``register()`` function can be used to automatically register the device
to the Coiote Server. Please note that if Certificate was used as a Security Mode
then the device public certificate should uploaded by hand in to the Coiote Server.

.. note::
   The self-signed certificates are generated to the ``cert`` folder.

We can now take a look at the provisioning tool implementation to see how this API can
be used:

.. highlight:: python
.. snippet-source:: tools/provisioning-tool/ptool.py


    try:
        fcty = fp.FactoryProvisioning(args.endpoint_cfg, args.URN, args.server,
                                      args.token, args.cert)
        if fcty.get_sec_mode() == 'cert' or fcty.get_sec_mode() == 'est':
            if args.scert is not None:
                fcty.set_server_cert(args.scert)

            if args.cert is not None:
                fcty.generate_self_signed_cert()
            elif args.pkey is not None and args.pcert is not None:
                fcty.set_endpoint_cert_and_key(args.pcert, args.pkey)

        fcty.provision_device()

        if args.server is not None and args.token is not None and args.URN is not None:
            fcty.register()

        ret_val = 0
    except ValueError as err:
        print('Incorrect configuration:', err)
    except ConnectionError as err:
        print('Coiote server error:', err)
    except requests.HTTPError as err:
        print(err)
    except OSError as err:
        print(err)
    except RuntimeError as err:
        print(err)
    except:
        print('Unexpected error, abort script execution')
    finally:
        sys.exit(ret_val)

First we create a object of the ``FactoryProvisioning`` class passing the arguments
provided to the script. Depending on the Security Mode set in the `endpoint_cfg`
we can generate a self signed certificate or pass the paths to the certificate for
both the client and server. Next we call ``provision_device()`` that will load the
configuration to the device. Finally we can call ``register()`` to automatically
register the device to the Coiote server. At the end of the script we will try
to catch all exceptions that could show up during script execution. The error
messages should give the user a hint what went wrong in case of any trouble.
