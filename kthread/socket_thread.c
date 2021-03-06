#include <linux/fs.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/seq_file_net.h>
#include <linux/socket.h>
#include <linux/uaccess.h>
#include <linux/uio.h>

#define MAX_PENDING 5
#define MESSAGE_MAX_LENGTH 256
#define SERVER_PORT 54321
#define MAX_CONNECTIONS 2

static struct task_struct *threadInfo;

static int initServerSocket(struct socket **newSocket,
struct sockaddr_in *serverAddress,unsigned int ipAddress,
unsigned short port) {
    int len = -1;

    len = sock_create_kern(&init_net,PF_INET,SOCK_STREAM,IPPROTO_TCP,
    newSocket);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not create socket.\n");
        return -1;
    }

    memset(serverAddress,0, sizeof(*serverAddress));
    serverAddress->sin_family = AF_INET;
    serverAddress->sin_port = htons(port);
    serverAddress->sin_addr.s_addr = htonl(ipAddress);

    len = (*newSocket)->ops->bind(*newSocket,
    (struct sockaddr *)serverAddress,sizeof(*serverAddress));
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not bind socket to address.\n");
        return -1;
    }
    len = (*newSocket)->ops->listen(*newSocket,MAX_PENDING);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not listen to socket.\n");
        return -1;
    }

    return 0;
}

static int acceptConnection(struct socket *serverSocket,
struct socket **newConnectionSocket, struct sockaddr_in *socketAddress) {
    int len = -1;
    (*newConnectionSocket)= sock_alloc();
    (*newConnectionSocket)->type = serverSocket->type;
    (*newConnectionSocket)->ops = serverSocket->ops;
    len = serverSocket->ops->accept(serverSocket,
    *newConnectionSocket, 0, false);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not connect.\n");
        return -1;
    }
    (*newConnectionSocket)->ops->getname(*newConnectionSocket,
    (struct sockaddr *)socketAddress,2);
    return 0;
}

static void configMessage(struct msghdr *message,struct kvec (*kv)[1],
struct iovec (*iov)[1],char (*messageBuffer)[MESSAGE_MAX_LENGTH],
unsigned long int messageLength,unsigned int mode) {
    message->msg_name = NULL;
    message->msg_namelen = 0;
    message->msg_control = NULL;
    message->msg_controllen = 0;
    message->msg_flags = MSG_DONTWAIT;
    message->msg_iocb = NULL;
    (*iov)[0].iov_len = messageLength;
    (*iov)[0].iov_base = *messageBuffer;
    iov_iter_init(&message->msg_iter,mode,*iov,1,1);
    (*kv)[0].iov_len = messageLength;
    (*kv)[0].iov_base = *messageBuffer;
}

static int sendMessage(struct socket *socketConnection,
char (*message)[MESSAGE_MAX_LENGTH],unsigned long int messageLength) {
    struct msghdr outputMessage;
    struct iovec iov[1];
    struct kvec kv[1];
    int len = -1;

    configMessage(&outputMessage,&kv,&iov,message,messageLength,WRITE
    );

    len = kernel_sendmsg(socketConnection,&outputMessage,kv,
    1,kv[0].iov_len);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not send message\n");
        return -1;
    }
    return len;
}

static int receiveMessage(struct socket *socketConnection,
char (*message)[MESSAGE_MAX_LENGTH]) {
    struct msghdr inputMessage;
    struct iovec iov[1];
    struct kvec kv[1];
    int len = -1;

    configMessage(&inputMessage,&kv,&iov,message,MESSAGE_MAX_LENGTH,
    READ);

    len = kernel_recvmsg(socketConnection,&inputMessage,kv,
    1,kv[0].iov_len,0);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not receive message.\n");
        return -1;
    }
    return len;
}

static int socketServer(void *unused) {
    unsigned int messageLength;
    char outputBuffer[MESSAGE_MAX_LENGTH],
    inputBuffer[MESSAGE_MAX_LENGTH];
    struct sockaddr_in serverAddress, clientAddress;
    struct socket *defaultSocket, *newConnectionSocket;
    int len = -1;
    int connectionCount = 0;

    printk(KERN_INFO "Server: Starting socket server...\n");
    printk(KERN_INFO "Server: Kernel thread created as: %s [PID=%d]\n",
    current->comm,current->pid);

    len = initServerSocket(&defaultSocket,&serverAddress,0x7F000001,
    SERVER_PORT);
    if (len < 0) {
        printk(KERN_INFO "Server: Error: Could not create server socket.\n");
        return -1;
    }

    while(connectionCount<MAX_CONNECTIONS) {
        printk(KERN_INFO "Server: Waiting for client connections.\n");
    
        len = acceptConnection(defaultSocket,&newConnectionSocket,
        &clientAddress);
        if (len < 0) {
            printk(KERN_INFO "Server: Error: Could not connect.\n");
            return -1;
        }

        printk(KERN_INFO "Server: Connection established.\n");
        printk(KERN_INFO "Server: Server ip and port are 0x%x and %u respectively\n",
        ntohl(clientAddress.sin_addr.s_addr),
	ntohs(clientAddress.sin_port));

	++connectionCount;

        len = receiveMessage(newConnectionSocket,&inputBuffer);
        if (len < 0) {
            printk(KERN_INFO "Sever: Error: Could not receive message.\n"
	    );
            return -1;
        }
        inputBuffer[len] = '\0';
        printk (KERN_INFO "Server: Client says: %s %d\n",
        inputBuffer, len);
  
        messageLength = 14;
        outputBuffer[0] = 'H'; outputBuffer[1] = 'e';
        outputBuffer[2] = 'l'; outputBuffer[3] = 'l';
        outputBuffer[4] = 'o'; outputBuffer[5] = ',';
        outputBuffer[6] = ' '; outputBuffer[7] = 'c';
        outputBuffer[8] = 'l'; outputBuffer[9] = 'i';
        outputBuffer[10] = 'e'; outputBuffer[11] = 'n';
        outputBuffer[12] = 't'; outputBuffer[13] = '!';

        printk(KERN_INFO "Server: Answering client.\n");

        len = sendMessage(newConnectionSocket,&outputBuffer,
        messageLength);
        if (len < 0) {
            printk(KERN_INFO "Server: Error: Could not send message.\n"
	    );
            return -1;
        }

        printk(KERN_INFO "Server: Message sent.\n");
    
        while(receiveMessage(newConnectionSocket,&inputBuffer));
        sock_release(newConnectionSocket);
        printk(KERN_INFO "Server: Client disconnected.\n");
    }

    sock_release(defaultSocket);
    printk(KERN_INFO "Server: Socket connection closed.\n");

    return 0;
}

static int __init initThread(void) {
    printk(KERN_INFO "Server: Dummy thread creation\n");
    threadInfo = kthread_create(socketServer,NULL,"Dummy Thread 1");
    if(IS_ERR(threadInfo)) {
        printk(KERN_INFO "Server: Failed creating the thread.");
    } else {
        printk(KERN_INFO "Server: Successfully created the thread.");
	wake_up_process(threadInfo);
    }
    return 0;
}

static void __exit threadCleanup(void) {
    printk(KERN_INFO "Server: Thread clean up.");
}

module_init(initThread);
module_exit(threadCleanup);
