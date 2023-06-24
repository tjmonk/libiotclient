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

/*!
 * @defgroup iotclient iotclient
 * @brief IOT Hub interface library
 * @{
 */

/*============================================================================*/
/*!
@file iotclient.c

    IOT Client API

    The IOT Client API is the Application Programming Interface to
    the IOT Hub Service.  Clients use this library to send messages to the
    IOT Hub without needing to be concerned with any of the details of the
    IOT connectivity.

*/
/*============================================================================*/


/*==============================================================================
        Includes
==============================================================================*/

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mqueue.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <stdbool.h>
#include <iotclient/iotclient.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! message queue name */
#define MESSAGE_QUEUE_NAME "/iothub"

/*! maximum IOTHUB message size */
#define MAX_MESSAGE_SIZE ( 256 * 1024 * 1024 )

/*==============================================================================
        Private type definitions
==============================================================================*/

/*! IOT Client connection state object */
struct IotClient
{
    /*! message queue used to send messages to the IOTHub service */
    mqd_t msgQ;

    /*! enable verbose output */
    bool verbose;

    /*! transmit message queue descriptor */
    mqd_t txMsgQ;

    /*! receive message queue descriptor */
    mqd_t rxMsgQ;

    /*! maximum size of the IOTHUB message headers */
    size_t maxMessageSize;

    /*! transmit buffer */
    char *txBuf;

    /*! receive buffer */
    char *rxBuf;

    /*! receive buffer size */
    size_t rxBufSize;

    /*! process PID used to create the data FIFO */
    pid_t pid;

    /*! name of the FIFO used to transfer the IOT message body */
    char *fifoName;
};

/*==============================================================================
        Private function declarations
==============================================================================*/

static int iotclient_CreateFIFO( IOTCLIENT_HANDLE hIoTClient );
static int iotclient_SendHeaders( IOTCLIENT_HANDLE hIoTClient,
                                  const char *headers);
static int iotclient_SendBody( IOTCLIENT_HANDLE hIoTClient,
                               const unsigned char *body,
                               size_t len );
static int iotclient_StreamBody( IOTCLIENT_HANDLE hIoTClient, int fd );

static void iotclient_DestroyFIFO( IOTCLIENT_HANDLE hIoTClient );
static int iotclient_CreateTxMessageQueue( IOTCLIENT_HANDLE hIoTClient );
static void iotclient_DestroyTxMessageQueue( IOTCLIENT_HANDLE hIoTClient );
static void iotclient_DestroyRxMessageQueue( IOTCLIENT_HANDLE hIoTClient );

static void iotclient_log( IOTCLIENT_HANDLE hIoTClient, char *msg );

/*==============================================================================
        File scoped variables
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

void __attribute__ ((constructor)) initLibrary(void) {
 //
 // Function that is called when the library is loaded
 //
}
void __attribute__ ((destructor)) cleanUpLibrary(void) {
 //
 // Function that is called when the library is »closed«.
 //
}

/*============================================================================*/
/*  IOTCLIENT_Create                                                          */
/*!
    Create a connection to the IOT Hub service

    The IOTCLIENT_Create function is used by IOT client applications
    to create a connection to the IOT Hub service to send and receive
    IOT messages via the IOT service.

    @retval a handle to the IOT server
    @retval NULL if the variable server could not be opened

==============================================================================*/
IOTCLIENT_HANDLE IOTCLIENT_Create( void )
{
    IOTCLIENT_HANDLE hIoTClient = NULL;
    int rc = EINVAL;

    /* create the connector to the IOT Client */
    hIoTClient = calloc( 1, sizeof( struct IotClient ) );
    if ( hIoTClient != NULL )
    {
        /* create the message queue */
        rc = iotclient_CreateTxMessageQueue( hIoTClient );
        if ( rc == EOK )
        {
            /* create the message body FIFO */
            rc = iotclient_CreateFIFO( hIoTClient );
            if ( rc != EOK )
            {
                /* clean up message queue */
                iotclient_DestroyTxMessageQueue( hIoTClient );
            }
        }

        if ( rc != EOK )
        {
            /* clean up IOT Client object */
            free( hIoTClient );
            hIoTClient = NULL;
        }
    }

    return hIoTClient;
}

/*============================================================================*/
/*  IOTCLIENT_Send                                                            */
/*!
    Send an IOT message via the IOT Hub service.

    The IOTCLIENT_Send function is used to send an IOT message
    to the cloud via the IOT Hub service.

    The message to be sent consists of message headers, and a message body.

    The message headers are a collection of key:value pairs, one per line
    separated by a newline.  The last header has an additional newline
    character to indicate that it is the last one.  The message headers
    are a text string and must be terminated with a NUL character.

    eg.

    my-header-1:value-1\n
    my-header-2:value-2\n\n

    The message body is an octet array.  It may contain binary or ASCII
    data.  It cannot exceed 256KB in length.

    @param[in]
        hIotClient
            handle to the IOT Client

    @param[in]
        headers
            pointer to a NUL terminated string containing the message headers

    @param[in]
        body
            pointer to an array of unsigned characters to send in the message
            body.  This can be a binary or ASCII array and does not need to
            be NUL terminated.

    @param[in]
        bodylen
            number of octets in the message body

    @retval EOK message delivered to IOTHub ingress queue
    @retval EINVAL invalid arguments
    @retval EMSGSIZE the message body or message headers are too big
    @retval EBADF invalid message queue descriptor

==============================================================================*/
int IOTCLIENT_Send( IOTCLIENT_HANDLE hIoTClient,
                    const char *headers,
                    const unsigned char *body,
                    size_t bodylen )
{
    int result = EINVAL;

    if ( ( hIoTClient != NULL ) &&
         ( headers != NULL ) &&
         ( body != NULL ) )
    {
        /* send the message header to the IOT Hub service */
        result = iotclient_SendHeaders( hIoTClient, headers );
        if ( result == EOK )
        {
            /* send the message body to the IOT Hub service */
            result = iotclient_SendBody( hIoTClient,
                                         body,
                                         bodylen);
        }
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_Stream                                                          */
/*!
    Stream an IOT message via the IOT Hub service.

    The IOTCLIENT_Stream function is used to stream an IOT message
    to the cloud via the IOT Hub service.

    The message to be sent consists of message headers, and a message body.

    The message headers are a collection of key:value pairs, one per line
    separated by a newline.  The last header has an additional newline
    character to indicate that it is the last one.  The message headers
    are a text string and must be terminated with a NUL character.

    eg.

    my-header-1:value-1\n
    my-header-2:value-2\n\n

    The message body is read as an octet stream from an open file
    descriptor.

    @param[in]
        hIotClient
            handle to the IOT Client

    @param[in]
        headers
            pointer to a NUL terminated string containing the message headers

    @param[in]
        fd
            file descriptor to stream data from

    @param[in]
        bodylen
            number of octets in the message body

    @retval EOK message delivered to IOTHub ingress queue
    @retval EINVAL invalid arguments
    @retval EMSGSIZE the message body or message headers are too big
    @retval EBADF invalid message queue descriptor

==============================================================================*/
int IOTCLIENT_Stream( IOTCLIENT_HANDLE hIoTClient,
                      const char *headers,
                      int fd )
{
    int result = EINVAL;

    if ( ( hIoTClient != NULL ) &&
         ( headers != NULL ) &&
         ( fd != -1 ) )
    {
        /* send the message header to the IOT Hub service */
        result = iotclient_SendHeaders( hIoTClient, headers );
        if ( result == EOK )
        {
            /* send the message body to the IOT Hub service */
            result = iotclient_StreamBody( hIoTClient, fd );
        }
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_CreateReceiver                                                  */
/*!
    Create and IOTCLIENT message receiver

    The IOTCLIENT_CreateReceiver function creates an IOTCLIENT message
    receiver which can received cloud-to-device messages from the IOTHUB
    service.

    @param[in]
        hIoTClient
            handle to the IOT Client which owns the message queue

    @param[in]
        name
            name of the receiver.

    @param[in]
        maxMessages
            maximum number of messages that can be queued

    @param[in]
        size
            max size of allowed command-to-device messages

    @retval EOK the message queue and message buffer were created
    @retval ENOMEM the message buffer could not be allocated
    @retval errno other message as reported by mq_open

==============================================================================*/
int IOTCLIENT_CreateReceiver( IOTCLIENT_HANDLE hIoTClient,
                              char *name,
                              int maxMessages,
                              size_t size )
{
    int result = EINVAL;
    struct mq_attr attr;
    char *receiver = NULL;

    attr.mq_maxmsg = maxMessages;
    attr.mq_msgsize = size;

    if ( hIoTClient != NULL )
    {
        /* initialize descriptors */
        hIoTClient->rxMsgQ = -1;

        /* build the message queue name */
        if ( asprintf( &receiver, "/%s", name ) > 0 )
        {
            /* allocate memory for the received messages */
            hIoTClient->rxBufSize = size;
            hIoTClient->rxBuf = calloc( 1, size );
            if ( hIoTClient->rxBuf != NULL )
            {
                /* create the receive message queue */
                hIoTClient->rxMsgQ = mq_open( receiver,
                                            O_RDONLY | O_CREAT,
                                            0666,
                                            &attr );
                result = ( hIoTClient->rxMsgQ != -1 ) ? EOK : errno;
            }
            else
            {
                result = ENOMEM;
            }

            free( receiver );
            receiver = NULL;
        }
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_Receive                                                         */
/*!
    Receive a message from the IOTHUB Service

    The IOTCLIENT_Receiver function waits for a received message from the
    IOTHUB service.  When the message is retrieved it is split into
    a set of message headers, and a message body component.

    @param[in]
        hIoTClient
            handle to the IOT Client which owns the message queue

    @param[in]
        name
            name of the receiver.

    @param[in]
        maxMessages
            maximum number of messages that can be queued

    @param[in]
        size
            max size of allowed command-to-device messages

    @retval EOK the message queue and message buffer were created
    @retval ENOMEM the message buffer could not be allocated
    @retval errno other message as reported by mq_open

==============================================================================*/
int IOTCLIENT_Receive( IOTCLIENT_HANDLE hIoTClient,
                       char **ppHeader,
                       char **ppBody,
                       size_t *pHeaderLength,
                       size_t *pBodyLength )
{
    int result = EINVAL;
    ssize_t n;
    unsigned int prio;
    char *p;
    size_t len;

    if( ( hIoTClient != NULL ) &&
        ( hIoTClient->rxMsgQ != -1 ) &&
        ( hIoTClient->rxBuf != NULL ) &&
        ( hIoTClient->rxBufSize > 0 ) &&
        ( ppHeader != NULL ) &&
        ( ppBody != NULL ) &&
        ( pHeaderLength != NULL ) &&
        ( pBodyLength != NULL ) )
    {
        printf("Waiting for data...\n");
        /* wait for a message on the receive queue */
        n = mq_receive( hIoTClient->rxMsgQ,
                        hIoTClient->rxBuf,
                        hIoTClient->rxBufSize,
                        &prio );

        printf("size: %ld\n", hIoTClient->rxBufSize);
        printf("rxBuf = %p\n", hIoTClient->rxBuf);
        printf("mq = %d\n", hIoTClient->rxMsgQ);
        printf("prio = %d\n", prio);
        printf("n = %ld\n", n);
        if ( n > 0 )
        {
            /* search for the start of the message body */
            p = strstr( hIoTClient->rxBuf, "\n\n");
            if( p == NULL )
            {
                /* no header data is included in the received message */
                printf("No header data\n");
                *ppHeader = NULL;
                *pHeaderLength = 0;
                *ppBody = hIoTClient->rxBuf;
                *pBodyLength = n;
            }
            else
            {
                /* NUL terminate the headers */
                *p = '\0';
                printf("buffer length = %ld\n", strlen( hIoTClient->rxBuf));
                printf("headers:\n");
                printf("%s\n", hIoTClient->rxBuf);
                /* calculate the header length */
                len = (void *)p - (void *)(hIoTClient->rxBuf);
                printf("p=%p\n", p);
                printf("header length=%ld\n",len);

                *ppHeader = hIoTClient->rxBuf;
                printf("*ppHeader = %s\n", *ppHeader);
                *pHeaderLength = len;

                /* skip over the header/body delimeter */
                p += 2;

                if( len < hIoTClient->rxBufSize )
                {
                    *ppBody = p;

                    /* calculate the body length */
                    len = n - len;

                    printf("body length = %ld\n", len);
                    *pBodyLength = len;
                }
                else
                {
                    printf("No body data\n");
                    /* no body data is included in the message */
                    *ppBody = NULL;
                    *pBodyLength = 0;
                }
            }

            result = EOK;
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_GetProperty                                                     */
/*!
    Get a property from the message headers

    The IOTCLIENT_GetProperty function searches for a property in the
    message headers given its name. If the property is found, its value
    is copied into the supplied buffer.

    If the property value is not found, or the size of its value exceeds
    the length of the supplied buffer, this function will return an
    error and the contents of the supplied buffer should be considered
    invalid and should not be used.

    @param[in]
        headers
            pointer to the message headers (NUL terminated string
            containing concatendated message properties in the following
            form: "name:value\n"

    @param[in]
        property
            name of the property to retrieve.

    @param[in]
        buf
            pointer to a buffer to receive the property value

    @param[in]
        len
            length of the buffer to hold the received value

    @retval EOK the property was found and its value retrieved
    @retval ENOENT the property was not found
    @retval ENOMEM the property length exceeded the supplied buffer length

==============================================================================*/
int IOTCLIENT_GetProperty( const char *headers,
                           char *property,
                           char *buf,
                           size_t len )
{
    int result = EINVAL;
    char *p;
    size_t plen;
    size_t i = 0;

    if ( ( headers != NULL ) &&
         ( property != NULL ) &&
         ( buf != NULL ) &&
         ( len > 0 ) )
    {
        /* get the length of the property we are searching for */
        plen = strlen(property);

        /* search for the property name in the headers */
        p = strstr( headers, property );

        /* if we fail from this point onwards it is because the property
           value length exceeds the available buffer length provided by
           the caller */
        result = ENOMEM;

        /* confirm what we found is a property name */
        if ( ( p != NULL ) && ( p[plen] == ':' ) )
        {
            /* point to the start of the proerty value */
            p = &p[plen+1];
            while( i < len )
            {
                if ( ( p[i] == '\n' ) || ( p[i] == '\0' ) )
                {
                    /* NUL terminate the returned property value */
                    buf[i] = 0;
                    result = EOK;
                }
                else
                {
                    buf[i] = p[i];
                }

                i++;
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_Close                                                           */
/*!
    Close the connection to the IOTHub service

    The IOTCLIENT_Close function closes the connection to the IOTHub
    service and frees up any resources used by the IOT Client connection.

    @param[in]
        hIoTClient
            handle to the IOT Client

    @retval EOK - the connection was successfully closed
    @retval EINVAL - an invalid IOT Client handle was specified

==============================================================================*/
int IOTCLIENT_Close( IOTCLIENT_HANDLE hIoTClient )
{
    int result = EINVAL;

    if ( hIoTClient != NULL )
    {
        iotclient_log( hIoTClient, "iotclient: closing");

        /* destroy the IOT FIFO */
        iotclient_DestroyFIFO( hIoTClient );

        /* destroy the IOT transmit message queue */
        iotclient_DestroyTxMessageQueue( hIoTClient );

        /* destroy the IOT receive message queue */
        iotclient_DestroyRxMessageQueue( hIoTClient );

        /* free the IoTClient object */
        free( hIoTClient );
        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  IOTCLIENT_SetVerbose                                                      */
/*!
    Set the IOT Client library verbosity level

    The IOTCLIENT_SetVerbose function enables or disables the IOT client
    verbose debug output.

    @param[in]
        hIoTClient
            handle to the IOT Client

    @param[in]
        verbose
            boolean to enable/disable verbose output

    @retval EOK - the verbose level was set
    @retval EINVAL - an invalid IOT Client handle was specified

==============================================================================*/
int IOTCLIENT_SetVerbose( IOTCLIENT_HANDLE hIoTClient, bool verbose )
{
    int result = EINVAL;

    if ( hIoTClient != NULL )
    {
        hIoTClient->verbose = verbose;

        iotclient_log( hIoTClient, "iotclient: verbose mode enabled");

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  iotclient_SendHeaders                                                     */
/*!
    Send IOT message headers via the IOT Hub service.

    The message headers are a collection of key:value pairs, one per line
    separated by a newline.  The last header has an additional newline
    character to indicate that it is the last one.  The message headers
    are a text string and must be terminated with a NUL character.

    eg.

    my-header-1:value-1\n
    my-header-2:value-2\n\n

    The message body is an octet array.  It may contain binary or ASCII
    data.  It cannot exceed 256KB in length.

    @param[in]
        hIotClient
            handle to the IOT Client

    @param[in]
        headers
            pointer to a NUL terminated string containing the message headers

    @retval EOK message delivered to IOTHub ingress queue
    @retval EINVAL invalid arguments
    @retval EMSGSIZE the message body or message headers are too big
    @retval EBADF invalid message queue descriptor

==============================================================================*/
static int iotclient_SendHeaders( IOTCLIENT_HANDLE hIoTClient,
                                  const char *headers )
{
    int result = EINVAL;
    size_t len;
    size_t totalLength;
    mqd_t q;
    const char preamble[] = "IOTC";
    char *txbuf;

    if ( ( hIoTClient != NULL ) &&
         ( hIoTClient->txBuf != NULL ) &&
         ( headers != NULL ) )
    {

        /* check the header size against the maximum message size
           allowing room for the preamble and PID of the client */
        len = strlen( headers );
        totalLength = len + 8;

        if( totalLength < hIoTClient->maxMessageSize )
        {
            /* construct the transmit buffer */
            /* preamble + pid + headers */
            txbuf = hIoTClient->txBuf;
            memcpy(txbuf, preamble, 4);
            memcpy(&txbuf[4], &(hIoTClient->pid), 4);
            memcpy(&txbuf[8], headers, len);

            /* get the message queue */
            q = hIoTClient->txMsgQ;
            if( q != (mqd_t)-1)
            {
                /* send the message */
                iotclient_log( hIoTClient, "iotclient: sending headers");
                if ( mq_send(q, txbuf, totalLength, 0) == 0 )
                {
                    result = EOK;
                }
                else
                {
                    result = errno;
                }
            }
            else
            {
                result = EBADF;
            }
        }
        else
        {
            result = EMSGSIZE;
        }
    }

    return result;
}

/*============================================================================*/
/*  iotclient_log                                                             */
/*!
    Generate a debug output log

    The iotclient_log function generates the specified debug output log
    if the debug logging is enabled via the verbose flag.

    @param[in]
        hIoTClient
            handle to the IOT Client

    @param[in]
        msg
            pointer to the NUL terminated character string to output

==============================================================================*/
static void iotclient_log( IOTCLIENT_HANDLE hIoTClient, char *msg )
{
    if ( hIoTClient != NULL )
    {
        if ( hIoTClient->verbose == true )
        {
            fprintf(stdout, "%s\n", msg );
        }
    }
}

/*============================================================================*/
/*  iotclient_CreateFIFO                                                      */
/*!
    Create a FIFO for sending IOT message bodies

    The iotclient_CreateFIFO function creates a FIFO which is used to
    stream IOT message body data to the IOTHUB for transmission.

    @param[in]
        hIoTClient
            handle to the IOT Client which will contain the FIFO reference

==============================================================================*/
static int iotclient_CreateFIFO( IOTCLIENT_HANDLE hIoTClient )
{
    int result = EINVAL;
    int n;

    if ( hIoTClient != NULL )
    {
        /* generate the FIFO name */
        hIoTClient->pid = getpid();
        n = asprintf( &hIoTClient->fifoName,
                      "/tmp/iothub_%d",
                      hIoTClient->pid );
        if ( ( n > 0 ) &&
             ( hIoTClient->fifoName != NULL ) )
        {
            /* create the FIFO */
            if( mkfifo( hIoTClient->fifoName, 0666) == 0 )
            {
                result = EOK;
            }
            else
            {
                iotclient_DestroyFIFO( hIoTClient );
                result = errno;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  iotclient_SendBody                                                        */
/*!
    Send an IOT message body to the IOT Hub Service

    The iotclient_SendBody function sends an IOT message body to the
    IOT Hub service via the IOT client write FIFO.

    @param[in]
        hIoTClient
            handle to the IOT Client containing the FIFO

    @param[in]
        body
            pointer to the message body data

    @param[in]
        len
            length of the message body to send

    @retval EOK the IOT message body was sent to the FIFO successfully
    @retval ENOENT the output FIFO name does not exist
    @retval EIO not all bytes were written
    @retval EMSGSIZE the message body exceeds the allowable size
    @retval other error as returned by write() or open()

==============================================================================*/
static int iotclient_SendBody( IOTCLIENT_HANDLE hIoTClient,
                               const unsigned char *body,
                               size_t len )
{
    int result = EINVAL;
    int fd;
    size_t n;

    if( ( hIoTClient != NULL ) &&
        ( body != NULL ) )
    {
        if( hIoTClient->fifoName != NULL )
        {
            /* open the output FIFO */
            fd = open( hIoTClient->fifoName, O_WRONLY );
            if( fd != -1 )
            {
                if( len < MAX_IOT_MSG_SIZE )
                {
                    /* send the body to the FIFO */
                    n = write( fd, body, len );
                    if( n == len )
                    {
                        result = EOK;
                    }
                    else if ( n == -1 )
                    {
                        result = errno;
                    }
                    else
                    {
                        /* did not write all expected data */
                        result = EIO;
                    }

                    /* close the output FIFO */
                    close( fd );
                }
                else
                {
                    /* message body is too large */
                    result = EMSGSIZE;
                }
            }
            else
            {
                /* unable to open the output FIFO */
                result = errno;
            }
        }
        else
        {
            /* FIFO Name is not defined */
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  iotclient_StreamBody                                                      */
/*!
    Stream an IOT message body to the IOT Hub Service

    The iotclient_StreamBody function streams an IOT message body to the
    IOT Hub service via the IOT client write FIFO.

    @param[in]
        hIoTClient
            handle to the IOT Client containing the FIFO

    @param[in]
        fd
            file descriptor to stream from

    @retval EOK the IOT message body was sent to the FIFO successfully
    @retval ENOENT the output FIFO name does not exist
    @retval EBADF invalid output stream
    @retval other error as returned by write() or open()

==============================================================================*/
static int iotclient_StreamBody( IOTCLIENT_HANDLE hIoTClient, int fd )
{
    int result = EINVAL;
    int fd_out;
    size_t n;
    unsigned char buf[BUFSIZ];
    size_t total = 0;
    size_t bytesLeft = MAX_IOT_MSG_SIZE;

    if( ( hIoTClient != NULL ) &&
        ( fd != -1 ) )
    {
        if( hIoTClient->fifoName != NULL )
        {
            /* assume everything is ok, until it isn't */
            result = EOK;

            /* open the output FIFO */
            fd_out = open( hIoTClient->fifoName, O_WRONLY );
            if( fd_out != -1 )
            {
                while( total < MAX_IOT_MSG_SIZE )
                {
                    /* read a block of data from the input */
                    n = read( fd, buf, BUFSIZ );
                    if ( n > bytesLeft )
                    {
                        /* truncate */
                        n = bytesLeft;
                    }

                    if ( n > 0 )
                    {
                        /* write the output buffer */
                        write( fd_out, buf, n );
                        printf("%.*s\n", (int)n, buf);
                        total += n;
                        bytesLeft -= n;
                    }
                    else
                    {
                        /* no more data */
                        break;
                    }
                }

                /* close the output FIFO */
                close( fd_out );
            }
            else
            {
                /* message body is too large */
                result = EBADF;
            }
        }
        else
        {
            /* FIFO Name is not defined */
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  iotclient_DestroyFIFO                                                     */
/*!
    Destroy a FIFO used for sending IOT message bodies

    The iotclient_DestroyFIFO function destroyes a FIFO which was used to
    stream IOT message body data to the IOTHUB for transmission.

    @param[in]
        hIoTClient
            handle to the IOT Client containing the FIFO

==============================================================================*/
static void iotclient_DestroyFIFO( IOTCLIENT_HANDLE hIoTClient )
{
    if ( hIoTClient != NULL )
    {
        if( hIoTClient->fifoName != NULL )
        {
            /* remove the message body FIFO */
            unlink( hIoTClient->fifoName );
            free( hIoTClient->fifoName );
            hIoTClient->fifoName = NULL;
        }
    }
}

/*============================================================================*/
/*  iotclient_CreateTxMessageQueue                                            */
/*!
    Create the IOTHub message queue and transmit buffer

    The iotclient_CreateTxMessageQueue function opens the IOTHUB message
    queue for writing, determines the maximum message size, and allocates
    a memory buffer for transmitting up to the maximum message size.

    @param[in]
        hIoTClient
            handle to the IOT Client which owns the message queue

    @retval EOK the message queue and message buffer were created
    @retval ENOMEM the message buffer could not be allocated
    @retval EIO could not get the queue's maximum message size
    @retval errno other message as reported by mq_open

==============================================================================*/
static int iotclient_CreateTxMessageQueue( IOTCLIENT_HANDLE hIoTClient )
{
    int result = EINVAL;
    struct mq_attr attr;

    if ( hIoTClient != NULL )
    {
        /* initialize descriptors */
        hIoTClient->txMsgQ = -1;

        /* open a connection to the IOTHUB message queue */
        hIoTClient->txMsgQ = mq_open( MESSAGE_QUEUE_NAME, O_WRONLY );
        if ( hIoTClient->txMsgQ != (mqd_t)-1 )
        {
            /* get the attributes */
            if ( mq_getattr(hIoTClient->txMsgQ, &attr) != -1 )
            {
                /* get the maximum message size */
                hIoTClient->maxMessageSize = attr.mq_msgsize;

                /* allocate memory for a transmit buffer */
                hIoTClient->txBuf = calloc( 1, attr.mq_msgsize );
                if( hIoTClient->txBuf != NULL )
                {
                    result = EOK;
                }
                else
                {
                    /* clean up the message queue */
                    mq_close( hIoTClient->txMsgQ );
                    hIoTClient->txMsgQ = -1;
                    result = ENOMEM;
                }
            }
            else
            {
                result = EIO;
            }
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

/*============================================================================*/
/*  iotclient_DestroyTxMessageQueue                                           */
/*!
    Clean up the transmit message queue and transmit buffer

    The iotclient_DestroyTxMessageQueue function cleans up the transmit
    message queue and deallocates the message transmit buffer.

    @param[in]
        hIoTClient
            handle to the IOT Client which owns the message queue

==============================================================================*/
static void iotclient_DestroyTxMessageQueue( IOTCLIENT_HANDLE hIoTClient )
{
    int result = EINVAL;

    if ( hIoTClient != NULL )
    {
        /* free the transmit buffer */
        if( hIoTClient->txBuf != NULL )
        {
            free( hIoTClient->txBuf );
            hIoTClient->txBuf = NULL;
        }

        /* close the message queue */
        if ( hIoTClient->txMsgQ != -1 )
        {
            mq_close( hIoTClient->txMsgQ );
            hIoTClient->txMsgQ = -1;
        }
    }
}

/*============================================================================*/
/*  iotclient_DestroyRxMessageQueue                                           */
/*!
    Clean up the receive message queue and receive buffer

    The iotclient_DestroyRxMessageQueue function cleans up the receive
    message queue and deallocates the message receive buffer.

    @param[in]
        hIoTClient
            handle to the IOT Client which owns the message queue

==============================================================================*/
static void iotclient_DestroyRxMessageQueue( IOTCLIENT_HANDLE hIoTClient )
{
    int result = EINVAL;

    if ( hIoTClient != NULL )
    {
        /* free the receive buffer */
        if( hIoTClient->rxBuf != NULL )
        {
            free( hIoTClient->rxBuf );
            hIoTClient->rxBuf = NULL;
        }

        /* close the message queue */
        if ( hIoTClient->rxMsgQ != -1 )
        {
            mq_close( hIoTClient->rxMsgQ );
            hIoTClient->rxMsgQ = -1;
        }
    }
}

/*! @}
 * end of the iotclient group */
