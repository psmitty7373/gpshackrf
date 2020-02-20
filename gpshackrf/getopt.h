#ifndef GETOPT_H
#define GETOPT_H

extern char *optarg;
extern int optreset;
extern int optind;

int getopt(int nargc, char * const nargv[], const char *ostr);

#endif
