all:
	g++ -lpthread -lglut -lGL -lportaudio -o noies src/core.cpp src/desktop/main.cpp src/desktop/mutex.cpp

clean:
	rm -f noies
