include ../GASNet/local/include/$(CONDUIT)-conduit/$(CONDUIT)-par.mak

# compiling flags here
CXXFLAGS = $(GASNET_CXXCPPFLAGS) $(GASNET_CXXFLAGS) -fPIC -Iinclude -std=c++11 -DUSE_GASNET
FFLAGS   = -fPIC -frecursive -J include

LDFLAGS  = 

# change these to set the proper directories where each files shoould be
SRCDIR   = src
OBJDIR   = build

SOURCES  := $(wildcard $(SRCDIR)/*.cpp)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
rm       = rm -Rf

all: CXXFLAGS += -O3
all: edat
	
debug: CXXFLAGS += -g
debug: edat

fortran: build_buildDir
	$(FTN) $(FFLAGS)  -o build/fedat.o -c include/edat.F90
	$(MPICXX) -shared -Wl,-soname,libfedat.so -o libfedat.so build/fedat.o
	ar rcs libfedat.a build/fedat.o
	
edat: build_buildDir $(OBJECTS)
	$(MPICXX) -shared -Wl,-soname,libedat.so -o libedat.so $(OBJECTS) $(GASNET_LDFLAGS) $(LDFLAGS) $(GASNET_LIBS)
	ar rcs libedat.a $(OBJECTS) $(LDFLAGS)

build_buildDir:
	@mkdir -p $(OBJDIR)

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.cpp
	$(MPICXX) $(CXXFLAGS) -c $< -o $@

.PHONEY: clean
clean:
	$(rm) $(OBJDIR)	
	$(rm) libedat.so
