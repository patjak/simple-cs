CC=gcc
CFLAGS=-Wall -g $(shell pkg-config --cflags glesv2 egl gbm)
LIBS=$(shell pkg-config --libs glesv2 egl gbm)
OBJS=main.o
TARGET=simple-cs

all: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) -o $(TARGET)

clean:
	rm -f *.o $(TARGET)
