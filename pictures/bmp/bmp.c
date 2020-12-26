#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main() {
	FILE *datafile = fopen("/mnt/c/data/stone_native_40k/res.bin", "rb");

	FILE *f;
	unsigned char *img = NULL;

	int w = 5000;
	int h = 5000;
	int image_size = 40000;

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
		for (int j = 0; j < image_size; j++) {
			fread(&value, 4, 1, datafile);
			int r = i + j + value;
			if (r > 0 && r < image_size) {
				int x = (int) (w / 2 - 0.5 * w * i / r + 0.5 * w * j / r);
				int y = (int) (0.866025 * ((w / 2 - 0.5 * w * i / r - 0.5 * w * j / r + 0.5 * w * value / r)));
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
	for (int i = 0; i < w; i++)
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

	img = (unsigned char*) malloc(3 * w * h);
	memset(img, 0, 3 * w * h);

	for (int i = 0; i < w; i++) {
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
			img[(x + y * w) * 3 + 2] = (unsigned char) (r);
			img[(x + y * w) * 3 + 1] = (unsigned char) (g);
			img[(x + y * w) * 3 + 0] = (unsigned char) (b);
		}

	}

	unsigned char bmpfileheader[14] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0,
			0, 0 };
	unsigned char bmpinfoheader[40] = { 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
			0, 24, 0 };
	unsigned char bmppad[3] = { 0, 0, 0 };

	bmpfileheader[2] = (unsigned char) (filesize);
	bmpfileheader[3] = (unsigned char) (filesize >> 8);
	bmpfileheader[4] = (unsigned char) (filesize >> 16);
	bmpfileheader[5] = (unsigned char) (filesize >> 24);

	bmpinfoheader[4] = (unsigned char) (w);
	bmpinfoheader[5] = (unsigned char) (w >> 8);
	bmpinfoheader[6] = (unsigned char) (w >> 16);
	bmpinfoheader[7] = (unsigned char) (w >> 24);
	bmpinfoheader[8] = (unsigned char) (h);
	bmpinfoheader[9] = (unsigned char) (h >> 8);
	bmpinfoheader[10] = (unsigned char) (h >> 16);
	bmpinfoheader[11] = (unsigned char) (h >> 24);

	f = fopen("img.bmp", "wb");
	fwrite(bmpfileheader, 1, 14, f);
	fwrite(bmpinfoheader, 1, 40, f);
	for (int i = 0; i < h; i++) {
		fwrite(img + (w * (h - i - 1) * 3), 3, w, f);
		fwrite(bmppad, 1, (4 - (w * 3) % 4) % 4, f);
	}

	free(img);
	fclose(f);
	return 0;
}
