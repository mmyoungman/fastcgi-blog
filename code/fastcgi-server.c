// Created by Mark Youngman 18 Feb 2018

#include "lib-mmy.h"
//#include <string.h> // for memset()
#include <sys/types.h>
#include <sys/wait.h> // for waitpid()
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h> // for close()

#define MYPORT "9000"
#define BACKLOG 10

// http://www.mit.edu/~yandros/doc/specs/fcgi-spec.html

typedef struct {
   unsigned char version;
   unsigned char type;
   unsigned char requestIdB1;
   unsigned char requestIdB0;
   unsigned char contentLengthB1;
   unsigned char contentLengthB0;
   unsigned char paddingLength;
   unsigned char reserved;
} fcgi_header;

typedef struct {
   unsigned char version;
   unsigned char type;
   unsigned char requestIdB1;
   unsigned char requestIdB0;
   unsigned char contentLengthB1;
   unsigned char contentLengthB0;
   unsigned char paddingLength;
   unsigned char reserved;
   unsigned char contentData[8];
} FCGI_Record;

typedef struct {
   unsigned char roleB1;
   unsigned char roleB0;
   unsigned char flags;
   unsigned char reserved[5];
} FCGI_BeginRequestBody;

int main()
{
   int status;
   int sockfd;
   struct addrinfo hints;
   struct addrinfo *servinfo;

   int newfd;
   struct sockaddr_storage clientaddr;
   socklen_t clientaddrsize;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   // TODO: Make sockets stuff more robust.
   status = getaddrinfo(NULL, MYPORT, &hints, &servinfo);
   if (status != 0)
   {
      dbg("%s", gai_strerror(status));
      return 1;
   }

   sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
   if (sockfd < 0)
   {
      perror("socket");
      return 1;
   }

   // INFO: Use setsockopt() here to handle "Address already in use" case when bind() is called.
   int yes = 1;
   status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
   if (status < 0)
   {
      perror("setsockopt");
      return 1;
   }

   status = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
   if (status < 0)
   {
      perror("bind");
      return 1;
   }

   freeaddrinfo(servinfo);

   status = listen(sockfd, BACKLOG);
   if (status < 0)
   {
      perror("listen");
      return 1;
   }
   dbg("Waiting for incoming connections...");

   while (1)
   {
      // INFO: accept(...) is blocking
      newfd = accept(sockfd, (struct sockaddr *)&clientaddr, &clientaddrsize);
      if (newfd < 0)
      {
         perror("accept");
         return 1;
      }
      pid_t pid = fork();
      if (pid < 0)
      {
         perror("fork");
         return 1;
      }
      if (pid != 0)
      {
         // INFO: This the parent process!

         // TODO: need to save child pid?
         // TODO: clean up zombie processes?

         waitpid(-1, NULL, WNOHANG);
         continue;
      }

      // TODO: child doesn't need sockfd, so free()?

      dbg("Connection accepted!");

      // TODO: Handle data spread over multiple packets
      int bufferLen = 4096;
      char buffer[bufferLen];
      while ((status = recv(newfd, &buffer, bufferLen, 0)) != 0)
      {
         if (status < 0)
         {
            perror("recv");
            return 1;
         }

         // TODO: Loop over fcgi records until all packets bytes have been consumed
         FCGI_Record *record;
         record = (FCGI_Record*) buffer;
         
         assert(record->version == 1);

         dbg("recv status: %d", status);

         dbg("version: %d", record->version);
         dbg("type: %d", record->type);

         u16 requestId = (record->requestIdB1 << 8) + record->requestIdB0;
         dbg("requestId: %d", requestId);

         u16 contentLength = (record->contentLengthB1 << 8) + record->contentLengthB0;
         dbg("contentLength: %d", contentLength);

         dbg("paddingLength: %d", record->paddingLength);
         dbg("reserved: %d", record->reserved);

         FCGI_BeginRequestBody *begin = (FCGI_BeginRequestBody*) &(record->contentData);
         u16 role = (begin->roleB1 << 8) + begin->roleB0;
         dbg("role: %d", role);
         dbg("flags: %d", begin->flags);

         unsigned char *ptr = (unsigned char*)record;
         ptr += 16; // jump to next fcgi record (8 bytes for fcgi header, 8 bytes of data)
         FCGI_Record *nextrecord = (FCGI_Record*) ptr;

         dbg("Should now go on to the FCGI_PARAMS record...");
         dbg("version: %d", nextrecord->version);
         dbg("type: %d", nextrecord->type);

         requestId = (nextrecord->requestIdB1 << 8) + nextrecord->requestIdB0;
         dbg("requestId: %d", requestId);

         contentLength = (nextrecord->contentLengthB1 << 8) + nextrecord->contentLengthB0;
         dbg("contentLength: %d", contentLength);

         dbg("paddingLength: %d", nextrecord->paddingLength);
         dbg("reserved: %d", nextrecord->reserved);

         ptr += 8;

         // TODO: Process name-value pairs: http://www.mit.edu/~yandros/doc/specs/fcgi-spec.html#S3.4
         for(int i = 0; i < 688; i++) {
            printf("%c", *ptr);
            ptr++;
         }

         // TODO: Send response packet
         //status = send(newfd, &buffer, status, 0);
         memset(buffer, 0, sizeof(char) * 4096);
      }

      dbg("Connection closed!");

      //shutdown(sockfd, 2);
      close(sockfd);
   }
}
