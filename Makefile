
all: mlekarna.o
	g++ -Wall -pedantic -o mlekarna mlekarna.o -lsimlib -lm
