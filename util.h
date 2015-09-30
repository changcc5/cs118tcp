#include <stdio.h>

void error(char *msg);

double chance();

void msg(const char *format, ...);

void teardown(FILE* file, int sockfd);