#include "lab.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef TEST
#define main main_exclude
#endif



int main(int argc, char **argv)
{
    return smtp_client_run(argc, argv);
}