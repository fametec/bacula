/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/

#ifdef HAVE_WIN32

# include <windows.h> 
# include <conio.h>
# include <stdbool.h>

#else  /* !HAVE_WIN32 */

# include <sys/stat.h>
# include <unistd.h>
# include <errno.h>
# include <stdlib.h>
# include <string.h>

#endif  /* HAVE_WIN32 */

/* Common include */
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#include "namedpipe.h"

#ifdef TEST_PROGRAM
# define Dmsg(level, ...) printf(__VA_ARGS__ ) 
#endif

#ifdef HAVE_WIN32

void namedpipe_init(NamedPipe *self)
{
   self->fd   = INVALID_HANDLE_VALUE;
   self->ifd  = -1;
}

void namedpipe_free(NamedPipe *self)
{
   if (self->fd != INVALID_HANDLE_VALUE) {
      CloseHandle(self->fd);
      self->fd = INVALID_HANDLE_VALUE;
      self->ifd = -1;
   }
}
#define BUFSIZE 8192
int namedpipe_create(NamedPipe *self, const char *path, mode_t mode)
{
   /* On windows,  */
   self->fd = CreateNamedPipeA( 
      path,                     // pipe name 
      PIPE_ACCESS_DUPLEX,       // read/write access 
      PIPE_TYPE_MESSAGE |       // message type pipe 
      PIPE_READMODE_MESSAGE |   // message-read mode 
      PIPE_WAIT,                // blocking mode 
      PIPE_UNLIMITED_INSTANCES, // max. instances  
      BUFSIZE,                  // output buffer size 
      BUFSIZE,                  // input buffer size 
      0,                        // client time-out 
      NULL);                    // default security attribute 
   
   if (self->fd == INVALID_HANDLE_VALUE) {
      Dmsg(10, "CreateNamedPipe failed, ERR=%d.\n", (int)GetLastError()); 
      return -1;
   }
   
   return 0;
}

int namedpipe_open(NamedPipe *self, const char *path, mode_t mode)
{
   bool fConnected=false;
   int  retry = 30;

   if (self->fd != INVALID_HANDLE_VALUE) { /* server mode */

      fConnected = ConnectNamedPipe(self->fd, NULL) ? 
         TRUE : (GetLastError() == ERROR_PIPE_CONNECTED); 

   } else {                               /* client mode */

      /* Need to wait for creation */
      while (retry-- > 0)
      { 
         self->fd = CreateFileA( 
            path,               // pipe name 
            GENERIC_WRITE | GENERIC_READ,
            0,              // no sharing 
            NULL,           // default security attributes
            OPEN_EXISTING,  // opens existing pipe 
            0,              // default attributes 
            NULL);          // no template file 
         
         // Break if the pipe handle is valid. 
         if (self->fd != INVALID_HANDLE_VALUE) {
            break; 
         }

         /* Wait a little bit for the other side to create the fifo */
         if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            Dmsg(10, "File not found, ERR=%d.\n", (int)GetLastError()); 
            Sleep(20000);
            continue;
         }

         // Exit if an error other than ERROR_PIPE_BUSY occurs. 
         if (GetLastError() != ERROR_PIPE_BUSY) {
            Dmsg(10, "CreateFile failed, ERR=%d.\n", 
                 (int)GetLastError()); 
            return -1;
         }
 
         // All pipe instances are busy, so wait for 20 seconds. 
         if (!WaitNamedPipeA(path, 20000)) { 
            Dmsg(10, "WaitNamedPipe failed, ERR=%d.\n", 
                 (int)GetLastError()); 
            return -1;
         } 
      } 
   }

   DWORD dwMode = PIPE_READMODE_MESSAGE; 
   
   fConnected = SetNamedPipeHandleState( 
      self->fd, // pipe handle 
      &dwMode,  // new pipe mode 
      NULL,     // don't set maximum bytes 
      NULL);    // don't set maximum time 

   if (!fConnected) {
      Dmsg(10, "SetNamedPipeHandleState failed, ERR=%d.\n", 
           (int)GetLastError()); 
   }

   if (fConnected) {
      int m = 0;
      if (mode & O_WRONLY || mode & O_APPEND) {
         m |= O_APPEND;
         
      } else if (mode & O_RDONLY) {
         m |= O_RDONLY;
      }
      self->ifd = _open_osfhandle((intptr_t)self->fd, m);
   }

   return self->ifd;
}


#else  /* !HAVE_WIN32 */

void namedpipe_init(NamedPipe *self)
{
   self->fd   = -1;
   self->ifd  = -1;
   self->name = NULL;
}

void namedpipe_free(NamedPipe *self)
{
   if (self->fd != -1) {
      close(self->fd);
      self->fd  = -1;
      self->ifd = -1;
   }
   if (self->name) {
      unlink(self->name);
      free(self->name);
      self->name = NULL;
   }
}

int namedpipe_create(NamedPipe *self, const char *path, mode_t mode)
{
   self->name = (char *)malloc(strlen(path) + 1);
   strcpy(self->name, path);

   if (mkfifo(path, mode) < 0 && errno != EEXIST) {
      return -1;
   }

   return 0;
}

int namedpipe_open(NamedPipe *self, const char *path, mode_t mode)
{
   self->ifd = self->fd = open(path, mode);
   return self->fd;
}

#endif  /* HAVE_WIN32 */

#ifdef TEST_PROGRAM

#include <string.h>
#include <stdlib.h>

#define BUF  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"

int main(int argc, char **argv)
{
   FILE *fp;
   NamedPipe p;
   char buf[65*1024], file[128];
   int fd;
   int mode;
   int n, m, o;
   if (argc < 4) {
      printf("Usage: %s client|server pipe file\n", argv[0]);
      exit(3);
   }

   namedpipe_init(&p);

   if (strcmp(argv[1], "server") == 0) {
      mode = O_WRONLY;
      if (namedpipe_create(&p, argv[2], 0600) < 0) {
         exit(2);
      }

   } else {
      mode = O_RDONLY;
   }

   printf("Trying to open %s mode=%d\n", argv[2], mode);
   fd = namedpipe_open(&p, argv[2], mode);

   if (fd < 0) {
      printf("Unable to open pipe\n");
      exit(1);
   }

   if (strcmp(argv[1], "server") == 0) {
      if (write(fd, BUF, strlen(BUF)+1) != strlen(BUF)+1) {
         printf("Unable to write data\n");
         exit(4);
      }
      
      if (write(fd, BUF, strlen(BUF)+1) != strlen(BUF)+1) {
         printf("Unable to write data\n");
         exit(4);
      }

      fp = bfopen(argv[3], "rb");
      if (!fp) {
         printf("Unable to open %s for reading\n", argv[3]);
         exit(4);
      }

      fseek(fp, 0, SEEK_END);
      m = ftell(fp);
      fseek(fp, 0, SEEK_SET);

      snprintf(buf, sizeof(buf), "%.10d\n", m);
      write(fd, buf, strlen(buf)+1);

      while (m > 0 && !feof(fp)) {
         n = fread(buf, 1, sizeof(buf), fp);
         Dmsg(000, "read %d from file\n", n);
         if (write(fd, buf, n) != n) {
            printf("Unable to write data from file\n");
            exit(5);
         }
         m -= n;
      }
      Dmsg(000, "EOF found\n");
      fclose(fp);

   } else {
      if ((n = read(fd, buf, sizeof(buf))) != strlen(BUF)+1) {
         Dmsg(000, "read failed (%d != %d), ERR=%d.\n", n, 
              (int)strlen(BUF)+1, errno); 
         exit(4);
      }
      if (read(fd, buf, sizeof(buf)) != strlen(BUF)+1) {
         Dmsg(000, "read failed, ERR=%d.\n", errno); 
         exit(4);
      }

      printf("buf=[%s]\n", buf);

      snprintf(file, sizeof(file), "%s.out", argv[3]);
      fp = bfopen(file, "wb");
      if (!fp) {
         printf("Unable to open %s for writing\n", buf);
         exit(4);
      }

      if ((n = read(fd, buf, sizeof(buf))) != 12) {
         Dmsg(000, "read failed (%d != %d), ERR=%d.\n", n, 12, errno); 
         exit(4);
      }

      m = atoi(buf);
      Dmsg(000, "will read %d from fifo\n", m);

      while (m > 0) {
         n = read(fd, buf, sizeof(buf));
         Dmsg(000, "Got %d bytes\n", n);
         if ((o = fwrite(buf, n, 1, fp)) != 1) {
            Dmsg(000, "write to file failed (%d != %d) ERR=%d.\n", o, n, errno); 
            exit(4);
         }
         m -= n;
      }
      fclose(fp);
   }

   namedpipe_free(&p);

   exit(0);
}
#endif
