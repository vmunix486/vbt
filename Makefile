CC=i686-w64-mingw32-gcc
CFLAGS=-Ofast -flto -std=c89 -pedantic -Wall -Wextra -static -static-libgcc
LIBS=-luser32 -lgdi32 -lshell32
MS=-mwindows

all:
	$(CC) main.c $(CFLAGS) $(LIBS) $(MS) -o battery.exe
