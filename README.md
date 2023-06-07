# libiotclient
iotclient library for the iothub service

## Overview

The iotclient library provides an API to the iothub service to allow
clients to send and receive messages to an Azure IoT Hub.

Clients create a connection to the iothub service using the
IOTCLIENT_Create function.  They can then send messages via
IOTCLIENT_Send, or stream data from a file descriptor using
IOTCLIENT_Stream.

Clients can also wait for received messages using the IOTCLIENT_Receive function.

When a client has finished interacting with the iothub
service, it can call the IOTCLIENT_Close function
to terminate the connection to the iothub service.

## Build

```
./build.sh
```
