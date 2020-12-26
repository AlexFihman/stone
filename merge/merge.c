#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main() {
   int fileno;
   FILE *datafile[30];
   FILE* outfile = fopen("/mnt/d/data/new/res.bin", "wb");

   for (fileno =0; fileno<30;fileno++){
        char filename[300];
        sprintf(filename, "/mnt/c/data/new/stone/res%d.bin", fileno*10000);
        datafile[fileno] = fopen(filename, "rb");
   }

   int value[10000];
   for (int line =0;line<300000;line++)
   for (fileno=0;fileno<30;fileno++) {
	fread(&value, sizeof(int), 10000, datafile[fileno]);
	fwrite(&value, sizeof(int), 10000, outfile);
   }
  
   for (fileno =0; fileno<30;fileno++){ fclose(datafile[fileno]); }
   fclose(outfile);
}