git clone https://github.com/ruthwik-coder/kmeans_
cd kmeans_
gcc kmeans.c -o kmeans.exe  -fopenmp -O3 -lmingw32 -lSDL3 -lSDL3_ttf && kmeans
