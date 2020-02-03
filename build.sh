rm vis*.o
rm vovu.o
rm vosses.so

go build -o vosses.so -buildmode=c-shared vosses.go
gcc -c -Wall -fpic vision.c -lm -lpthread -lrt -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math -pipe -O3
gcc -c -Wall vovu.c -o vovu.o -lm -lpthread -lrt -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math -pipe -O3
gcc vision.o vovu.o -o visionon  -lm -lpthread -lrt ./vosses.so -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math -pipe -O3

