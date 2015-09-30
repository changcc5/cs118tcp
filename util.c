#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

void error(char *msg)
{
   perror(msg);
   exit(1);
}

double chance()
{
   return (double)rand() / (double)RAND_MAX;
}

void msg(const char *format, ...)
{
   char s[100];

   time_t systime;
   systime = time(NULL);
   struct tm *p = localtime(&systime);

   strftime(s, 100, "%Y-%m-%d %T", p);
   printf("%s: ", s);
   
   va_list arg;
   va_start (arg, format);
   vprintf (format, arg);
   va_end (arg);
}

void teardown(FILE* file, int sockfd)
{
   msg("Tearing down connection\n");
   if (file != NULL)
      fclose(file);
   close(sockfd);
   exit(0);
}