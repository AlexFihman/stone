png: png.o lodepng.o
	gcc -O3 -std=c11 -mtune=native png.o lodepng.o -o png -lm
png.o: png.c
	gcc -c -O3 -std=c11 -mtune=native png.c
lodepng.o: lodepng.c lodepng.h
	gcc -c -O3 -std=c11 -mtune=native lodepng.c
clean:
	rm -rf *.o png
