all: target

target:
	g++ -O3 -o legalizer legalizer.cpp


clean:
	rm -f legalizer