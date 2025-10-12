#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


/************************************************************************/
/* checkoutput.c checks if a file is sorted                             */
/*                                                                      */
/* usage:  ./makeinput recsize file                                     */
/*             where                                                    */
/*               recsize:  size of a record in bytes - must be mult(4)  */
/*               file:     name of the file that is to be checked       */
/************************************************************************/
int main(int argc, char* argv[])
{
  FILE *fin;
  int recSize; 
  int buffer[1000];
  int i, old;

  /* initialize values */
  recSize = atoi(argv[1]);
  
  fin = fopen64(argv[2], "r");
  for (i = 0; !feof(fin); i++)
  {
    fread(buffer, recSize, 1, fin);  
    if (feof(fin))  break;

    if ((i > 0) && (buffer[0] < old))  printf("Not sorted: record %d\n", i);
    old = buffer[0];
  }
  fclose(fin);
}


