NAME	:= noies
SOURCES := src src/desktop
LIBS    := -lpthread -lglut -lGL -lportaudio

CPPFILES := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
HFILES   := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.h))

$(NAME): $(CPPFILES) $(HFILES)
	g++ $(LIBS) -o $@ $(CPPFILES)

clean:
	rm -f $(NAME)
