# ============================================================================
#  Makefile CFWR                             Chris Plumberg, September 8, 2015
# ============================================================================
##
##  Environments :	MAIN	= 	main sourcefile	
##
##  Usage : 	(g)make	[all]		compile the whole project		
##			install	make all and copy binary to $INSTPATH
##			clean		remove objectfiles in obj_$TYPE 
##			distclean	remove all objectsfiles and binaries
##  

# HDF5 flags
export HDF5_CXX=$(shell ../../crank/configure_compiler_C++.sh)
export HDF5_CXXLINKER=$(shell ../../crank/configure_compiler_C++.sh)
export HDF5_CLINKER=$(shell ../../crank/configure_compiler_C++.sh)

# Set compiler and flags
CC := h5c++
CFLAGS= $(shell ../../crank/configure_compiler_C++_Flags.sh)
CFLAGS += -lgsl -lgslcblas -g

# Various directories and definitions
RM          =   rm -f
O           =   .o
LDFLAGS     =   $(CFLAGS)
SYSTEMFILES =   $(SRCGNU)
INCDIR      =    include
SRCDIR      =    src
OBJDIR      =    obj


# --------------- Files involved ------------------

ifeq "$(MAIN)" ""
MAIN		=	cfwr.e
endif

MAINSRC     =   main.cpp

SRC		=	$(SRCDIR)/lib.cpp \
			$(SRCDIR)/cbessik.cpp \
			$(SRCDIR)/cbessjy.cpp \
			$(SRCDIR)/bessik.cpp \
			$(SRCDIR)/bessjy.cpp \
			$(SRCDIR)/gamma.cpp \
			$(SRCDIR)/readindata.cpp \
			$(SRCDIR)/cfwr.cpp \
			$(SRCDIR)/cfwr_HDF.cpp \
			$(SRCDIR)/cfwr_admin.cpp \
			$(SRCDIR)/cfwr_exact.cpp \
			$(SRCDIR)/cfwr_resonance.cpp \
			$(SRCDIR)/cfwr_IO.cpp \
			$(SRCDIR)/cfwr_GFroutines.cpp \
			$(SRCDIR)/Arsenal.cpp \
			$(SRCDIR)/gauss_quadrature.cpp \
			$(SRCDIR)/ParameterReader.cpp \
			$(SRCDIR)/chebyshev.cpp

INC		= 	main.h \
			$(INCDIR)/parameters.h \
			$(INCDIR)/readindata.h \
			$(INCDIR)/cfwr.h \
			$(INCDIR)/Arsenal.h \
			$(INCDIR)/Stopwatch.h \
			$(INCDIR)/cfwr_lib.h \
			$(INCDIR)/gauss_quadrature.h \
			$(INCDIR)/generate_processing_record.h \
			$(INCDIR)/lib.h \
			$(INCDIR)/bessel.h \
			$(INCDIR)/ParameterReader.h \
			$(INCDIR)/chebyshev.h \
			$(INCDIR)/chebyshev_library.h \
			$(INCDIR)/fastexp.h
#			$(INCDIR)/cast.h \
#			$(INCDIR)/sse.h \
#			$(INCDIR)/fasttrig.h

# -------------------------------------------------

SRCFILES 	= 	$(SRC) $(INC) GNUmakefile
OBJECTS				=	$(addprefix $(OBJDIR)/, $(addsuffix $O, \
						$(notdir $(basename $(SRC)))))
OBJECTS				+=	$(addsuffix $O, $(basename $(MAINSRC)))
TARGET		=	$(MAIN)
INSTPATH	=	..

# --------------- Pattern rules -------------------

./%.o: ./%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	-@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/%.cpp:
	if [ -f $@ ] ; then touch $@ ; else false ; fi

$(TARGET):	$(OBJECTS)	
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) 

# -------------------------------------------------

.PHONY:		all mkobjdir clean distclean install

all:		mkobjdir $(TARGET)

help:
		@grep '^##' GNUmakefile

mkobjdir:	
		-@mkdir -p $(OBJDIR)

clean:		
		-rm $(OBJECTS)

distclean:	
		-rm $(TARGET)
		-rm $(OBJECTS)

install:	$(TARGET)
		cp $(TARGET) $(INSTPATH)

# --------------- Dependencies -------------------
$(SRCDIR)/Arsenal.cpp:         $(INCDIR)/Arsenal.h
$(SRCDIR)/ParameterReader.cpp: $(INCDIR)/ParameterReader.h \
                               $(INCDIR)/Arsenal.h
$(SRCDIR)/readindata.cpp:      $(INCDIR)/readindata.h \
                               $(INCDIR)/lib.h
$(SRCDIR)/lib.cpp:             $(INCDIR)/lib.h
$(SRCDIR)/cfwr_HDF.cpp:        $(INCDIR)/cfwr.h \
                               $(INCDIR)/cfwr_lib.h \
                               $(INCDIR)/lib.h
$(SRCDIR)/cfwr_admin.cpp:      $(INCDIR)/cfwr.h \
                               $(INCDIR)/Arsenal.h \
                               $(INCDIR)/gauss_quadrature.h \
                               $(INCDIR)/cfwr_lib.h \
                               $(INCDIR)/lib.h \
                               $(INCDIR)/parameters.h
$(SRCDIR)/cfwr_IO.cpp:         $(INCDIR)/cfwr.h \
                               $(INCDIR)/Arsenal.h \
                               $(INCDIR)/gauss_quadrature.h \
                               $(INCDIR)/lib.h
$(SRCDIR)/cfwr_GFroutines.cpp: $(INCDIR)/cfwr.h \
                               $(INCDIR)/Arsenal.h \
                               $(INCDIR)/gauss_quadrature.h \
                               $(INCDIR)/Stopwatch.h \
                               $(INCDIR)/cfwr_lib.h \
                               $(INCDIR)/lib.h
$(SRCDIR)/cfwr_resonance.cpp:  $(INCDIR)/cfwr.h \
                               $(INCDIR)/Arsenal.h \
                               $(INCDIR)/gauss_quadrature.h \
                               $(INCDIR)/cfwr_lib.h \
                               $(INCDIR)/lib.h \
                               $(INCDIR)/Stopwatch.h \
                               $(INCDIR)/fastexp.h
$(SRCDIR)/cfwr.cpp:            $(INCDIR)/cfwr.h \
                               $(INCDIR)/Arsenal.h \
                               $(INCDIR)/gauss_quadrature.h \
                               $(INCDIR)/Stopwatch.h \
                               $(INCDIR)/cfwr_lib.h \
                               $(INCDIR)/lib.h
./main.cpp:                    main.h \
                               $(INCDIR)/Stopwatch.h \
                               $(INCDIR)/parameters.h \
                               $(INCDIR)/readindata.h \
                               $(INCDIR)/cfwr.h \
                               $(INCDIR)/ParameterReader.h \
                               $(INCDIR)/generate_processing_record.h \
                               $(INCDIR)/lib.h \
                               $(INCDIR)/chebyshev.h

