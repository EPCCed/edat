CC       = mpicxx
# compiling flags here
CFLAGS   = -fPIC -Iinclude -std=c++11 

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

noopt: CFLAGS += -Wall
noopt: edat	

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
	$(rm) libedat.so libedat.a
