all:
	gcc .\src\simulator.c -o sim -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf
	gcc .\src\traffic_generator.c -o tra_gen -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf
