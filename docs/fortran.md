# Fortran bindings
EDAT provides bindings for Fortran 2003 so you can call EDAT from your Fortran code (fairly) seamlessly. This page details the extra compilation steps needed to build Fortran support and the modified API calls required. Note that these rely on the ISO C bindings and hence require F2003 (this aspect of the standard is well supported by all major compilers.)

## Building Fortran bindings
After building EDAT you must then build the Fortran bindings by executing `make fortran` on the EDAT makefile. This will build two things, _edat.mod_ file in the _include_ directory (which is the module imported by the Fortran code) and also a fortran library (_libfedat.so_ and _libfedat.a_) in the top level directory. This extra library contains functionality required to interface between your Fortran code and the EDAT C library and must also be linked against by your executable (it does not replace the main EDAT library, so you link against it in addition to _libedat_ .)

## Using EDAT in Fortran codes
Firstly you must import the edat module
```f90
use edat
use iso_c_binding, only : c_ptr, c_int
```
