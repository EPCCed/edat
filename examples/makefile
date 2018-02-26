CC       = mpicc
# compiling flags here
CFLAGS   = 

LFLAGS   = -L../ -ledat

# change these to set the proper directories where each files shoould be
SRCDIR   = src
OBJDIR   = build

rm       = rm -f

%.o: %.c
	$(CC) $(CFLAGS) -I../include -c $< -o $@ 

all: example1

example1: src/example_1.o
	$(CC) -o example_1 src/example_1.o $(LFLAGS)

.PHONEY: clean
clean:
	$(rm) src/*.o example_1
