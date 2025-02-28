#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import socket
import random
import math
from argparse import ArgumentParser

SOCKET_PATH = "/tmp/lwm2m-gateway.sock"


def run_end_device_simulator():
    # Argument parser setup
    parser = ArgumentParser(
        description="Simulate an end device comunnicating with the LwM2M Gateway")
    parser.add_argument(
        '-d',
        '--device-id',
        help="Specify the device ID (e.g., urn:dev:12345). If not provided, a random one will be generated.",
        default=None)

    args = parser.parse_args()

    device_id = ""
    if args.device_id:
        device_id = args.device_id
        print("End device ID provided: ", device_id)
        max_length = len("urn:dev:XXXXX")
        if len(device_id) > max_length:
            print(f"End device ID is too long - the maximum length is {max_length} characters")
            return
    else:
        random_int = random.randint(1, 65534)
        device_id = f"urn:dev:{random_int:05}"
        print("New random generated end device ID: ", device_id)

    rand_temp = lambda: round(random.uniform(0, 100), 2)
    temperature = rand_temp()
    max_temperature = temperature
    min_temperature = temperature

    try:
        client_socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        client_socket.connect(SOCKET_PATH)
        print("Connected to the Gateway")

        while True:
            # Wait for a message from the server
            server_message = client_socket.recv(65535)
            if server_message == b"get temperature":
                print("Sending temperature")
                temperature = rand_temp()
                if temperature > max_temperature:
                    max_temperature = temperature
                if temperature < min_temperature:
                    min_temperature = temperature
                client_socket.sendall(str(temperature).encode())
            elif  server_message == b"get id":
                # Message format: "urn:dev:XXXXX"
                print("Sending device ID")
                client_socket.sendall(device_id.encode())
            elif server_message == b"get max":
                print("Sending max temperature")
                client_socket.sendall(str(round(max_temperature, 2)).encode())
            elif server_message == b"get min":
                print("Sending min temperature")
                client_socket.sendall(str(round(min_temperature, 2)).encode())
            elif server_message == b"reset":
                print("Resetting max and min temperature")
                max_temperature = temperature
                min_temperature = temperature
                client_socket.sendall(b"OK")
            else:
                raise Exception("Unexpected message from server")

    except KeyboardInterrupt:
        print("End device simulator stopped")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client_socket.close()


if __name__ == "__main__":
    run_end_device_simulator()
