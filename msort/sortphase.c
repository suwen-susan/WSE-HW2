#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

int intCompare(int *r1, int *r2)
{ if (*r1 > *r2) return(1); else if (*r1 < *r2) return(-1); else return(0); } 

/************************************************************************/
/* sortphase.c implements the first phase of an I/O-efficient mergesort */
/*      It is assumed that the first 4 bytes of each record contain an  */
/*      integer key by which sorting occurs, and that records are of a  */
/*      fixed size that is a multiple of 4 bytes. It creates a number   */
/*      of sorted output files of size up to memsize. Output files have */
/*      filenames created by adding a running number to a given prefix, */
/*      and the list of these filenames is written to another file.     */
/*                                                                      */
/* usage:  ./sortphase recsize memsize infile outfileprefix foutlist    */
/*             where                                                    */
/*               recsize:  size of a record in bytes - must be mult(4)  */
/*               memsize:  size of available memory in bytes            */
/*               infile:  name of the input file                        */
/*               outfileprefix:  prefix (including path and name) used  */
/*                               to generate numbered temp output files */
/*               foutlist:  file to which names of temp files written   */
/************************************************************************/
int main(int argc, char* argv[])
{
  FILE *fin, *fout, *foutlist;
  unsigned char *buf;        /* buffer holding records */
  int recSize, bufSize;      /* size of a record, and # of records in buffer */ 
  int numRecs, numFiles = 0; /* number of records, and # of output files */
  char filename[1024];
  int i;

  recSize = atoi(argv[1]);
  buf = (unsigned char *) malloc(atoi(argv[2]));
  bufSize = atoi(argv[2]) / recSize;
  
  fin = fopen64(argv[3], "r");
  foutlist = fopen64(argv[5], "w");

  while (!feof(fin))
  {
    /* read data until buffer full or input file finished */
    for (numRecs = 0; numRecs < bufSize; numRecs++)
    {
      fread(&(buf[numRecs*recSize]), recSize, 1, fin);  
      if (feof(fin))  break;
    }

    /* create output filename, store sorted data, then store the filename */
    if (numRecs > 0)
    {
      sprintf(filename, "%s%d", argv[4], numFiles);;
      fout = fopen64(filename, "w");
      qsort((void *)(buf), numRecs, recSize, intCompare);
      for (i = 0; i < numRecs; i++)
        fwrite(&buf[i*recSize], recSize, 1, fout);  
      fclose(fout);
      fprintf(foutlist, "%s\n", filename);
      numFiles++;
    }
  }
 
  fclose(fin);
  fclose(foutlist);
  free(buf);
}

