all:
	g++ -lglut -lGL -o noies src/main.cpp

clean:
	rm noies
