stone20: main.o square.o
	g++ -O3 -std=c++11 -mtune=native main.o square.o -o stone20
main.o: main.cpp
	g++ -c -O3 -std=c++11 -mtune=native main.cpp
square.o: square.cpp
	g++ -c -O3 -std=c++11 -mtune=native square.cpp
clean:
	rm -rf *.o stone20 result.bin
