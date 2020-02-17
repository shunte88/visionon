CFLAGS= -Wall -O3
L_CL_FLAGS= -Wall -lm -lpthread -lrt -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math -pipe -03
L_SW_FLAGS= -Wall -lssl -lcrypto -lm -lpthread -lrt -march=armv7-a -mtune=cortex-a7 -mfloat-abi=hard -mfpu=neon-vfpv4 -ffast-math -pipe -O3
COMPILER=gcc

all: visionon

visionon: vovu.o kiss_fft.o vision.o log.o cdata.o chat.o cio.o
	$(COMPILER) $(+) -o $(@) $(L_SW_FLAGS)

vovu.o: vovu.c vovu.h vision.h log.h kiss_fft.h cdata.h chat.h cio.h
	$(COMPILER) -c $(<) -o $(@) $(L_SW_FLAGS)

kiss_fft.o: kiss_fft.c kiss_fft.h 
	$(COMPILER) -c $(<) -o $(@) $(CCFLAGS)

vision.o: vision.c vision.h vovu.h kiss_fft.h log.h
	$(COMPILER) -c $(<) -o $(@) $(L_CL_CFLAGS)

log.o: log.c log.h vovu.h
	$(COMPILER) -c $(<) -o $(@) $(CFLAGS)

cdata.o: cdata.c cdata.h vovu.h
	$(COMPILER) -c $(<) -o $(@) $(L_SW_FLAGS)

chat.o: chat.c chat.h vovu.h
	$(COMPILER) -c $(<) -o $(@) $(L_SW_FLAGS)

cio.o: cio.c cio.h vovu.h
	$(COMPILER) -c $(<) -o $(@) $(L_SW_FLAGS)


clean:
	rm *.o;rm ./visionon


