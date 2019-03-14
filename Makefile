all:
	g++ -lpthread -lglut -lGL -lportaudio -o noies src/main.cpp

clean:
	rm -f noies
