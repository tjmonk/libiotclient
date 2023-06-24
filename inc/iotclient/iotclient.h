/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

#ifndef IOTCLIENT_H
#define IOTCLIENT_H

/*==============================================================================
        Includes
==============================================================================*/

#include <stddef.h>

/*==============================================================================
        Public Definitions
==============================================================================*/

/* define the success return code */
#ifndef EOK
#define EOK 0
#endif

/*! maximum size of an IOT Message */
#define MAX_IOT_MSG_SIZE  ( 256 * 1024 * 1024 )

/*! opaque pointer to the IOT Client */
typedef struct IotClient *IOTCLIENT_HANDLE;

/*==============================================================================
        Public Function Declarations
==============================================================================*/

/*! create a new IOT Client */
IOTCLIENT_HANDLE IOTCLIENT_Create();

/*! send a message to the IOTHub service */
int IOTCLIENT_Send( IOTCLIENT_HANDLE hIoTClient,
                    const char *headers,
                    const unsigned char *body,
                    size_t bodylen );

/*! stream a message to the IOTHUB service */
int IOTCLIENT_Stream( IOTCLIENT_HANDLE hIoTClient,
                      const char *headers,
                      int fd );

/*! Get a message property from the message headers */
int IOTCLIENT_GetProperty( const char *headers,
                           char *property,
                           char *buf,
                           size_t len );

/*! create a message receiver */
int IOTCLIENT_CreateReceiver( IOTCLIENT_HANDLE hIoTClient,
                              char *name,
                              int maxMessages,
                              size_t size );

/*! receive a cloud-to-device message */
int IOTCLIENT_Receive( IOTCLIENT_HANDLE hIoTClient,
                       char **ppHeader,
                       char **ppBody,
                       size_t *pHeaderLength,
                       size_t *pBodyLength );

/*! close the IOT Client */
int IOTCLIENT_Close( IOTCLIENT_HANDLE hIoTClient );

/*! enable/disable verbose output */
int IOTCLIENT_SetVerbose( IOTCLIENT_HANDLE hIoTClient, bool verbose );

#endif
