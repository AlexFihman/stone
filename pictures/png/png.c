#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lodepng.h"

int main() {
	FILE *datafile = fopen("/mnt/d/data/new/data/stone/data/res.bin", "rb");
	//FILE *datafile = fopen("/mnt/c/data/stone_native_20k/res.bin","rb");

	FILE *f;
	unsigned char *img = NULL;

	long long int w = 27000;
	long long int h = 27000;
	
	//int image_size = 40000;
	long long int image_size = 300000;

	int filesize = 54 + 3 * w * h; //w is your image width, h is image height, both int

	int **rawdata_r = (int**) malloc(w * sizeof(int*));
	int **rawdata_g = (int**) malloc(w * sizeof(int*));
	int **rawdata_b = (int**) malloc(w * sizeof(int*));

	for (int i = 0; i < w; i++) {
		rawdata_r[i] = (int*) malloc(h * sizeof(int));
		rawdata_g[i] = (int*) malloc(h * sizeof(int));
		rawdata_b[i] = (int*) malloc(h * sizeof(int));
		memset(rawdata_r[i], 0, h * sizeof(int));
		memset(rawdata_g[i], 0, h * sizeof(int));
		memset(rawdata_b[i], 0, h * sizeof(int));
	}

	int value;
	for (int i = 0; i < image_size; i++) {
		if (i%1000==0) printf("i=%d\n", i);
		for (int j = 0; j < image_size; j++) {
			fread(&value, 4, 1, datafile);
			double r = i + j + value;
			r *= 1.001;
			if (r > 0 && r < image_size) {
				int x = (int) (w / 2 - 0.5 * w * i / r + 0.5 * w * j / r);
				int y = (int) (0.866025 * ((w / 2 - 0.5 * w * i / r - 0.5 * w * j / r + 0.5 * w * value / r)));
				//printf("x= %d, y=%d\n", x, y);
				if (r < image_size * 0.3333) {
					rawdata_r[x][y]++;
				} else if (r < image_size * 0.6666) {
					rawdata_g[x][y]++;
				} else {
					rawdata_b[x][y]++;
				}
			}
		}
	}
	double max_r = 1;
	double max_g = 1;
	double max_b = 1;
	for (int i = 0; i < w; i++) {
		printf("i=%d\n", i);
		for (int j = 0; j < w; j++) {
			if (rawdata_r[i][j] > 0)
				if (max_r < (log(rawdata_r[i][j]) + 1.0))
					max_r = log(rawdata_r[i][j]) + 1.0;

			if (rawdata_g[i][j] > 0)
				if (max_g < (log(rawdata_g[i][j]) + 1.0))
					max_g = log(rawdata_g[i][j]) + 1.0;

			if (rawdata_b[i][j] > 0)
				if (max_b < (log(rawdata_b[i][j]) + 1.0))
					max_b = log(rawdata_b[i][j]) + 1.0;

		}
        }

	img = (unsigned char*) malloc(3 * w * h);
	memset(img, 0, 3 * w * h);

	for (int i = 0; i < w; i++) {
		if (i%1000==0) printf("making img, i=%d\n", i);
		for (int j = 0; j < w; j++) {
			int x = i;
			int y = (h - 1) - j;

			int r = 0;
			int g = 0;
			int b = 0;

			if (rawdata_r[i][j] > 0) {
				r = (int) (255.99 * (log(rawdata_r[i][j]) + 1.0) / max_r);
			}
			if (rawdata_g[i][j] > 0) {
				g = (int) (255.99 * (log(rawdata_g[i][j]) + 1.0) / max_g);
			}

			if (rawdata_b[i][j] > 0) {
				b = (int) (255.99 * (log(rawdata_b[i][j]) + 1.0) / max_b);
			}

			if (r > 255)
				r = 255;
			if (g > 255)
				g = 255;
			if (b > 255)
				b = 255;
			img[(x + y * w) * 3ULL + 2] = (unsigned char) (r);
			img[(x + y * w) * 3ULL + 1] = (unsigned char) (g);
			img[(x + y * w) * 3ULL + 0] = (unsigned char) (b);
		}

	}
	printf("before  png");
	lodepng_encode24_file("res.png", img, w, h);
}
