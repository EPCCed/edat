# Fortran bindings
EDAT provides bindings for Fortran 2003 so you can call EDAT from your Fortran code (fairly) seamlessly. This page details the extra compilation steps needed to build Fortran support and the modified API calls required. Note that these rely on the ISO C bindings and hence require F2003 (this aspect of the standard is well supported by all major compilers.)

## Building Fortran bindings
After building EDAT you must then build the Fortran bindings by executing `make fortran` on the EDAT makefile. This will build two things, _edat.mod_ file in the _include_ directory (which is the module imported by the Fortran code) and also a fortran library (_libfedat.so_ and _libfedat.a_) in the top level directory. This extra library contains functionality required to interface between your Fortran code and the EDAT C library and must also be linked against by your executable (it does not replace the main EDAT library, so you link against it in addition to _libedat_ .)

## Using EDAT in Fortran codes
Firstly you must import the edat module and also the ISO C bindings module (a current limitation of the Fortran bindings is that there is some leakage of the C side into the user's Fortran code, this is only minor and just provides the type signatures for tasks.)

```f90
program test_edat
  use edat, only : edatInit, edatFinalise
  use iso_c_binding, only : c_ptr, c_int
implicit none
  call edatInit()
  call edatFinalise()
end program
```

In the code snippet here we are importing EDAT and the ISO C bindings. Then we initialise EDAT and immediately finalise it. Regardless of your usage of EDAT, only the _c_ptr_ and _c_int_ types from _iso_c_binding_ are needed, therefore we suggest just popping this _use_ line in after EDAT and that's fine.

## Writing tasks in Fortran
The API of the Fortran code is fairly similar to the C API, but with a couple of noteworthy differences. Firstly when firing an event, you can fire vectors or scalars directly (there is no need to create a variable and pass the reference as in the C API.) When firing events, payload data of all common types (integers, logicals, characters, floats etc...) can be passed directly.

The biggest difference between the Fortran and C API is within the task itself. When the task is called the events and number of events are C constructs and as such need to be converted into Fortran data types. This is done via the _getEvents_ procedure which will convert the events into an array of _EDAT_Event_ derived types. To access payload data, you need to do this via the appropriately typed pointer member, these are described in the following table. The specific member used is driven by the type of the payload data and other members will still be unassociated.

EDAT Event member | Fortran type
----------------- | ------------
int_data | integer, pointer, dimension(:)
byte_data | character, pointer, dimension(:)
float_data | real(kind=4), pointer, dimension(:)
double_data | real(kind=8), pointer, dimension(:)
long_data | integer(kind=8), pointer, dimension(:)

__Note:__ You must declare your EDAT task subrouties as recursive or compile the code with recursive subroutines enabled. This is so that Fortran uses distinct stack spaces for each invocation (as different instances of the same task may be running concurrently) rather than just using a single stack space which is often the default behaviour.

```f90
program test_edat
  use edat
  use iso_c_binding, only : c_ptr, c_int
implicit none
  call edatInit()
  if (edatGetRank() == 0) then
    call edatFireEvent(12, EDAT_INT, 1, 1, "hello")
  else if (edatGetRank() == 1) then
    call edatSubmitTask(myTask, 1, 0, "hello")
  end if
  call edatFinalise()
  
contains

  recursive subroutine myTask(events, number_events)
    type(c_ptr), intent(in), target :: events
    integer(c_int), value, intent(in) :: number_events	

    type(EDAT_Event) :: processed_events(number_events)
    call getEvents(events, number_events,  processed_events)

    print *, processed_events(1)%int_data, associated(processed_events(1)%float_data)	
  end subroutine myTask
end program
```
A full EDAT Fortran program is illustrated above. The task will display the integer payload data (in this case the value _12_ and _F_ indicating that the _float_data_ pointer member is not associated.) 

## Configuring EDAT in code
It is also possible to provide configuration options to EDAT from Fortran code when initialising it, this is done via the _edatInitWithConfiguration()_ API call.

```f90
character (len=65), Allocatable :: keys(:), values(:)

allocate(keys(1), values(1))
keys(1)="EDAT_REPORT_WORKER_MAPPING"
values(1)="true"

call edatInitWithConfiguration(1, keys, values)
deallocate(keys, values)
```
This is illustrated in the code snippet above, where key and value character arrays are allocated and the options passed to EDAT on initialisation.
