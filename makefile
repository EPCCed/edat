CC       = mpicxx
FTN = mpif90
# compiling flags here
CFLAGS   = -fPIC -Iinclude -std=c++11 
FFLAGS = -fPIC -frecursive -J include

LFLAGS   =

# change these to set the proper directories where each files shoould be
SRCDIR   = src
OBJDIR   = build

SOURCES  := $(wildcard $(SRCDIR)/*.cpp)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
rm       = rm -Rf

all: CFLAGS += -O3
all: edat
	
debug: CFLAGS += -g
debug: edat

fortran: build_buildDir
	$(FTN) $(FFLAGS)  -o build/fedat.o -c include/edat.F90
	$(CC) -shared -Wl,-soname,libfedat.so -o libfedat.so build/fedat.o
	ar rcs libfedat.a build/fedat.o
	
edat: build_buildDir $(OBJECTS)
	$(CC) -shared -Wl,-soname,libedat.so -o libedat.so $(OBJECTS) $(LFLAGS)
	ar rcs libedat.a $(OBJECTS) $(LFLAGS)

build_buildDir:
	@mkdir -p $(OBJDIR)

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

.PHONEY: clean
clean:
	$(rm) $(OBJDIR)	
	$(rm) libedat.so
