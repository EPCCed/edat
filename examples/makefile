CC       = mpicc
# compiling flags here
CFLAGS   = -g

LFLAGS   = -L../ -ledat

# change these to set the proper directories where each files shoould be
SRCDIR   = src
OBJDIR   = build

rm       = rm -f

%.o: %.c
	$(CC) $(CFLAGS) -I../include -c $< -o $@ 

all: example1 example2 example3 example4 example5 example6

example1: src/example_1.o
	$(CC) -o example_1 src/example_1.o $(LFLAGS)

example2: src/example_2.o
	$(CC) -o example_2 src/example_2.o $(LFLAGS)

example3: src/example_3.o
	$(CC) -o example_3 src/example_3.o $(LFLAGS)

example4: src/example_4.o
	$(CC) -o example_4 src/example_4.o $(LFLAGS)

example5: src/example_5.o
	$(CC) -o example_5 src/example_5.o $(LFLAGS)

example6: src/example_6.o
	$(CC) -o example_6 src/example_6.o $(LFLAGS)

.PHONEY: clean
clean:
	$(rm) src/*.o example_1 example_2 example_3 example_4 example_5 example_6
