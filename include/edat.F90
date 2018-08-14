module edat
  use iso_c_binding, only : c_int, c_ptr, c_char, c_loc, c_funloc, c_f_pointer, C_NULL_CHAR, C_NULL_PTR
  implicit none

  private

  integer, parameter :: EDAT_NOTYPE=0, EDAT_NONE=0, EDAT_INT=1, EDAT_FLOAT=2, EDAT_DOUBLE=3, EDAT_BYTE=4, &
    EDAT_ADDRESS=5, EDAT_LONG=6, EDAT_ALL=-1, EDAT_ANY=-2, EDAT_SELF=-3

  type, bind(c) :: EDAT_Metadata_c
    integer(c_int) :: data_type, number_elements, source
    type(c_ptr) :: event_id
  end type

  type, bind(c) :: EDAT_Event_c
    type(c_ptr) :: data
    type(EDAT_Metadata_c) :: metadata
  end type

  type :: EDAT_Metadata
    integer :: data_type, number_elements, source
    character(len=100) :: event_id
  end type

  type :: EDAT_Event
    integer, pointer, dimension(:) :: int_data
    character, pointer, dimension(:) :: byte_data
    real(kind=4), pointer, dimension(:) :: float_data
    real(kind=8), pointer, dimension(:) :: double_data
    integer(kind=8), pointer, dimension(:) :: long_data
    type(EDAT_Metadata) :: metadata
  end type

  abstract interface
    subroutine edatTask(events, number_events)
      use iso_c_binding, only : c_int, c_ptr
      type(c_ptr), intent(in), target :: events
      integer(c_int), value, intent(in) :: number_events
    end subroutine
  end interface

  interface
    subroutine edatInit_c() bind(C, name="edatInit")
    end subroutine edatInit_c

    subroutine edatInitWithConfiguration_c(num_entries, k, v) bind(C, name="edatInitWithConfiguration")
      use iso_c_binding, only : c_int, c_ptr
      implicit none
      integer(c_int), value :: num_entries
      type(c_ptr) :: k, v
    end subroutine edatInitWithConfiguration_c

    integer(c_size_t) function strlen(str) bind(C, name="strlen")
      use iso_c_binding, only : c_ptr, c_size_t
      type(c_ptr), value :: str
    end function

    subroutine edatInitialiseWithCommunicator_c(comm) bind(C, name="edatInitialiseWithCommunicator")
      use iso_c_binding, only : c_int
      integer(c_int), value :: comm
    end subroutine edatInitialiseWithCommunicator_c

    subroutine edatFinalise_c() bind(C, name="edatFinalise")
    end subroutine edatFinalise_c

    integer function edatGetRank_c() bind(C, name="edatGetRank")
    end function edatGetRank_c

    integer function edatGetNumRanks_c() bind(C, name="edatGetNumRanks")
    end function edatGetNumRanks_c

    subroutine edatFireEvent_c(user_data, data_type, data_count, target_rank, event_id) bind(C, name="edatFireEvent")
      use iso_c_binding, only : c_int, c_ptr, c_char
      type(c_ptr), value :: user_data
      integer(c_int), value :: data_type, data_count, target_rank
      character(c_char) :: event_id
    end subroutine edatFireEvent_c

    subroutine edatFirePersistentEvent_c(user_data, data_type, data_count, target_rank, &
      event_id) bind(C, name="edatFirePersistentEvent")

      use iso_c_binding, only : c_int, c_ptr, c_char
      type(c_ptr), value :: user_data
      integer(c_int), value :: data_type, data_count, target_rank
      character(c_char) :: event_id
    end subroutine edatFirePersistentEvent_c

    subroutine edatScheduleTask_c(task, task_name, number_dependencies, ranks, event_ids, &
      persistent, greedy) bind(C, name="edatScheduleTask_f")

      use iso_c_binding, only : c_int, c_ptr, c_funptr, c_bool

      type(c_funptr), value :: task
      integer(c_int), value :: number_dependencies
      type(c_ptr), value :: task_name
      integer(c_int) :: ranks(:)
      type(c_ptr), value :: event_ids
      logical(c_bool), value :: persistent, greedy
    end subroutine edatScheduleTask_c

    integer function edatIsTaskScheduled_c(task_name) bind(C, name="edatIsTaskScheduled")
      use iso_c_binding, only : c_ptr

      type(c_ptr), value :: task_name
    end function edatIsTaskScheduled_c

    integer function edatDescheduleTask_c(task_name) bind(C, name="edatDescheduleTask")
      use iso_c_binding, only : c_ptr

      type(c_ptr), value :: task_name
    end function edatDescheduleTask_c

    subroutine edatLock_c(lock_name) bind(C, name="edatLock")
      use iso_c_binding, only : c_ptr

      type(c_ptr), value :: lock_name
    end subroutine edatLock_c

    subroutine edatUnlock_c(lock_name) bind(C, name="edatUnlock")
      use iso_c_binding, only : c_ptr

      type(c_ptr), value :: lock_name
    end subroutine edatUnlock_c

    integer function edatTestLock_c(lock_name) bind(C, name="edatTestLock")
      use iso_c_binding, only : c_ptr

      type(c_ptr), value :: lock_name
    end function edatTestLock_c

    subroutine edatLockComms_c() bind(C, name="edatLockComms")
    end subroutine edatLockComms_c

    subroutine edatUnlockComms_c() bind(C, name="edatUnlockComms")
    end subroutine edatUnlockComms_c
  end interface

  interface edatFireEvent
    module procedure edatFireEvent_character_array, edatFireEvent_integer_array, edatFireEvent_float_array, &
      edatFireEvent_double_array, edatFireEvent_long_array, edatFireEvent_integer, edatFireEvent_long, &
      edatFireEvent_float, edatFireEvent_double, edatFireEvent_nodata
  end interface edatFireEvent

  interface edatFirePersistentEvent
    module procedure edatFirePersistentEvent_character_array, edatFirePersistentEvent_integer_array, &
      edatFirePersistentEvent_float_array, edatFirePersistentEvent_double_array, edatFirePersistentEvent_long_array, &
      edatFirePersistentEvent_integer, edatFirePersistentEvent_long, edatFirePersistentEvent_float, &
      edatFirePersistentEvent_double
  end interface edatFirePersistentEvent

  public EDAT_NOTYPE, EDAT_NONE, EDAT_INT, EDAT_FLOAT, EDAT_DOUBLE, EDAT_BYTE, EDAT_ADDRESS, EDAT_LONG, EDAT_ALL, &
    EDAT_ANY, EDAT_SELF, EDAT_Event, EDAT_Metadata, edatInit, edatInitWithConfiguration, edatFinalise, &
    edatGetRank, edatGetNumRanks, edatFireEvent, edatScheduleTask, edatScheduleNamedTask, edatSchedulePersistentTask, &
    edatSchedulePersistentNamedTask, edatSchedulePersistentGreedyTask, edatSchedulePersistentNamedGreedyTask, &
    edatDescheduleTask, edatIsTaskScheduled, edatLock, edatUnlock, edatTestLock, getEvents, &
    edatInitialiseWithCommunicator, edatLockComms, edatUnlockComms
contains

  subroutine getEvents(events, number_events, processed_events)
    type(c_ptr), intent(in), target :: events
    integer, intent(in) :: number_events
    type(EDAT_Event), dimension(number_events), intent(inout)  :: processed_events

    type(EDAT_Event_c), pointer, dimension(:) :: raw_events
    character, pointer, dimension(:) :: c
    integer :: i, j, event_id_len, data_type, number_elements

    call c_f_pointer(c_loc(events), raw_events, [number_events])

    do i=1, number_events
      event_id_len=strlen(raw_events(i)%metadata%event_id)
      call c_f_pointer(raw_events(i)%metadata%event_id, c, [event_id_len])
      processed_events(i)%metadata%event_id=""
      do j=1, event_id_len
        processed_events(i)%metadata%event_id(j:j)=c(j)
      end do
      processed_events(i)%metadata%source=raw_events(i)%metadata%source
      data_type=raw_events(i)%metadata%data_type
      number_elements=raw_events(i)%metadata%number_elements
      processed_events(i)%metadata%data_type=data_type
      processed_events(i)%metadata%number_elements=number_elements

      if (data_type == EDAT_INT) then
        call c_f_pointer(raw_events(i)%data, processed_events(i)%int_data, [number_elements])
      else if (data_type == EDAT_FLOAT) then
        call c_f_pointer(raw_events(i)%data, processed_events(i)%float_data, [number_elements])
      else if (data_type == EDAT_DOUBLE) then
        call c_f_pointer(raw_events(i)%data, processed_events(i)%double_data, [number_elements])
      else if (data_type == EDAT_LONG) then
        call c_f_pointer(raw_events(i)%data, processed_events(i)%long_data, [number_elements])
      else if (data_type == EDAT_BYTE) then
        call c_f_pointer(raw_events(i)%data, processed_events(i)%byte_data, [number_elements])
      end if
    end do
  end subroutine getEvents

  subroutine edatInit()
    call edatInit_c()
  end subroutine edatInit

  subroutine edatLockComms()
    call edatLockComms_c()
  end subroutine edatLockComms

  subroutine edatUnlockComms()
    call edatUnlockComms_c()
  end subroutine edatUnlockComms

  subroutine edatInitialiseWithCommunicator(comm)
    integer, intent(in) :: comm

    call edatInitialiseWithCommunicator_c(comm)
  end subroutine edatInitialiseWithCommunicator

  subroutine edatInitWithConfiguration(num_entries, keys, values)
    integer, intent(in) :: num_entries
    character(len=*), intent(in) :: keys(num_entries), values(num_entries)

    type(c_ptr) :: pass_keys(num_entries), pass_values(num_entries)
    character(len=c_char), dimension(:,:), pointer :: each_key, each_value
    character(64) :: string_value
    integer :: i, j

    allocate(each_key(65, num_entries), each_value(65, num_entries))

    do i=1, num_entries
      string_value=trim(keys(i))
      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_key(j,i)=string_value(j:j)
      end do
      each_key(j,i)=C_NULL_CHAR

      string_value=trim(values(i))
      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_value(j,i)=string_value(j:j)
      end do
      each_value(j,i)=C_NULL_CHAR

      pass_keys(i)=c_loc(each_key(1,i))
      pass_values(i)=c_loc(each_value(1,i))
    end do

    call edatInitWithConfiguration_c(num_entries, pass_keys, pass_values)
    deallocate(each_key, each_value)
  end subroutine

  subroutine edatFinalise()
    call edatFinalise_c()
  end subroutine edatFinalise

  integer function edatGetRank()
    edatGetRank=edatGetRank_c()
  end function edatGetRank

  integer function edatGetNumRanks()
    edatGetNumRanks=edatGetNumRanks_c()
  end function edatGetNumRanks

  logical function edatIsTaskScheduled(task_name)
    character(len=*), intent(in) :: task_name

    character(len=c_char), dimension(:), pointer :: task_name_processed
    character(100) :: string_value
    integer :: j

    allocate(task_name_processed(100))

    string_value=trim(task_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      task_name_processed(j)=string_value(j:j)
    end do
    task_name_processed(j)=C_NULL_CHAR

    edatIsTaskScheduled=merge(.true., .false., edatIsTaskScheduled_c(c_loc(task_name_processed)) == 1)
    deallocate(task_name_processed)
  end function edatIsTaskScheduled

  logical function edatDescheduleTask(task_name)
    character(len=*), intent(in) :: task_name

    character(len=c_char), dimension(:), pointer :: task_name_processed
    character(100) :: string_value
    integer :: j

    allocate(task_name_processed(100))

    string_value=trim(task_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      task_name_processed(j)=string_value(j:j)
    end do
    task_name_processed(j)=C_NULL_CHAR

    edatDescheduleTask=merge(.true., .false., edatDescheduleTask_c(c_loc(task_name_processed)) == 1)
    deallocate(task_name_processed)
  end function edatDescheduleTask

  subroutine edatLock(lock_name)
    character(len=*), intent(in) :: lock_name

    character(len=c_char), dimension(:), pointer :: lock_name_processed
    character(100) :: string_value
    integer :: j

    allocate(lock_name_processed(100))

    string_value=trim(lock_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      lock_name_processed(j)=string_value(j:j)
    end do
    lock_name_processed(j)=C_NULL_CHAR

    call edatLock_c(c_loc(lock_name_processed))
    deallocate(lock_name_processed)
  end subroutine edatLock

  subroutine edatUnlock(lock_name)
    character(len=*), intent(in) :: lock_name

    character(len=c_char), dimension(:), pointer :: lock_name_processed
    character(100) :: string_value
    integer :: j

    allocate(lock_name_processed(100))

    string_value=trim(lock_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      lock_name_processed(j)=string_value(j:j)
    end do
    lock_name_processed(j)=C_NULL_CHAR

    call edatUnlock_c(c_loc(lock_name_processed))
    deallocate(lock_name_processed)
  end subroutine edatUnlock

  logical function edatTestLock(lock_name)
    character(len=*), intent(in) :: lock_name

    character(len=c_char), dimension(:), pointer :: lock_name_processed
    character(100) :: string_value
    integer :: j

    allocate(lock_name_processed(100))

    string_value=trim(lock_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      lock_name_processed(j)=string_value(j:j)
    end do
    lock_name_processed(j)=C_NULL_CHAR

    edatTestLock=merge(.true., .false., edatTestLock_c(c_loc(lock_name_processed)) == 1)
    deallocate(lock_name_processed)
  end function edatTestLock

  subroutine edatScheduleTask(task, number_dependencies, eA_rank, eA_id, eB_rank, eB_id, eC_rank, eC_id, eD_rank, eD_id, &
    eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies))

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), C_NULL_PTR, number_dependencies, ranks, c_loc(event_ids), false_flag , false_flag)
    deallocate(each_eid, ranks, event_ids)
  end subroutine edatScheduleTask

  subroutine edatScheduleNamedTask(task, task_name, number_dependencies, eA_rank, eA_id, eB_rank, &
      eB_id, eC_rank, eC_id, eD_rank, eD_id, eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    character(len=*), intent(in) :: task_name
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    character(len=c_char), dimension(:), pointer :: task_name_processed
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies), &
      task_name_processed(100))

    string_value=trim(task_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      task_name_processed(j)=string_value(j:j)
    end do
    task_name_processed(j)=C_NULL_CHAR

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), c_loc(task_name_processed), number_dependencies, ranks, &
      c_loc(event_ids), false_flag , false_flag)
    deallocate(each_eid, ranks, event_ids, task_name_processed)
  end subroutine edatScheduleNamedTask

  subroutine edatSchedulePersistentTask(task, number_dependencies, eA_rank, eA_id, eB_rank, eB_id, &
    eC_rank, eC_id, eD_rank, eD_id, eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false., true_flag = .true.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies))

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), C_NULL_PTR, number_dependencies, ranks, c_loc(event_ids), true_flag , false_flag)
    deallocate(each_eid, ranks, event_ids)
  end subroutine edatSchedulePersistentTask

  subroutine edatSchedulePersistentNamedTask(task, task_name, number_dependencies, eA_rank, eA_id, eB_rank, &
      eB_id, eC_rank, eC_id, eD_rank, eD_id, eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    character(len=*), intent(in) :: task_name
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    character(len=c_char), dimension(:), pointer :: task_name_processed
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false., true_flag=.true.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies), &
      task_name_processed(100))

    string_value=trim(task_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      task_name_processed(j)=string_value(j:j)
    end do
    task_name_processed(j)=C_NULL_CHAR

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), c_loc(task_name_processed), number_dependencies, ranks, &
      c_loc(event_ids), true_flag , false_flag)
    deallocate(each_eid, ranks, event_ids, task_name_processed)
  end subroutine edatSchedulePersistentNamedTask

  subroutine edatSchedulePersistentGreedyTask(task, number_dependencies, eA_rank, eA_id, eB_rank, eB_id, &
      eC_rank, eC_id, eD_rank, eD_id, eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false., true_flag = .true.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies))

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), C_NULL_PTR, number_dependencies, ranks, c_loc(event_ids), true_flag , true_flag)
    deallocate(each_eid, ranks, event_ids)
  end subroutine edatSchedulePersistentGreedyTask

  subroutine edatSchedulePersistentNamedGreedyTask(task, task_name, number_dependencies, eA_rank, eA_id, eB_rank, &
      eB_id, eC_rank, eC_id, eD_rank, eD_id, eE_rank, eE_id, eF_rank, eF_id, eG_rank, eG_id, eH_rank, eH_id)
    procedure(edatTask) :: task
    character(len=*), intent(in) :: task_name
    integer, intent(in) :: number_dependencies
    integer, intent(in), optional :: eA_rank, eB_rank, eC_rank, eD_rank, eE_rank, eF_rank, eG_rank, eH_rank
    character(len=*), intent(in), optional :: eA_id, eB_id, eC_id, eD_id, eE_id, eF_id, eG_id, eH_id

    type(c_ptr), pointer :: event_ids(:)
    character(len=c_char), dimension(:,:), pointer :: each_eid
    character(len=c_char), dimension(:), pointer :: task_name_processed
    integer(kind=c_int), pointer :: ranks(:)
    character(100) :: string_value
    integer :: i, j
    logical :: arg_present
    logical(kind=1) :: false_flag = .false., true_flag=.true.

    allocate(each_eid(100, number_dependencies), ranks(number_dependencies), event_ids(number_dependencies), &
      task_name_processed(100))

    string_value=trim(task_name)
    string_value=adjustl(string_value)
    do j=1, len(trim(string_value))
      task_name_processed(j)=string_value(j:j)
    end do
    task_name_processed(j)=C_NULL_CHAR

    do i=1, number_dependencies
      if (i == 1) then
        arg_present=present(eA_rank) .and. present(eA_id)
        if (arg_present) then
          ranks(i)=eA_rank
          string_value=trim(eA_id)
        end if
      else if (i == 2) then
        arg_present=present(eB_rank) .and. present(eB_id)
        if (arg_present) then
          ranks(i)=eB_rank
          string_value=trim(eB_id)
        end if
      else if (i == 3) then
        arg_present=present(eC_rank) .and. present(eC_id)
        if (arg_present) then
          ranks(i)=eC_rank
          string_value=trim(eC_id)
        end if
      else if (i == 4) then
        arg_present=present(eD_rank) .and. present(eD_id)
        if (arg_present) then
          ranks(i)=eD_rank
          string_value=trim(eD_id)
        end if
      else if (i == 5) then
        arg_present=present(eE_rank) .and. present(eE_id)
        if (arg_present) then
          ranks(i)=eE_rank
          string_value=trim(eE_id)
        end if
      else if (i == 6) then
        arg_present=present(eF_rank) .and. present(eF_id)
        if (arg_present) then
          ranks(i)=eF_rank
          string_value=trim(eF_id)
        end if
      else if (i == 7) then
        arg_present=present(eG_rank) .and. present(eG_id)
        if (arg_present) then
          ranks(i)=eG_rank
          string_value=trim(eG_id)
        end if
      else if (i == 8) then
        arg_present=present(eH_rank) .and. present(eH_id)
        if (arg_present) then
          ranks(i)=eH_rank
          string_value=trim(eH_id)
        end if
      end if
      if (.not. arg_present) then
        print *, "Error: Event rank or ID not present for dependency ", i
        stop -1
      end if

      string_value=adjustl(string_value)
      do j=1, len(trim(string_value))
        each_eid(j,i)=string_value(j:j)
      end do
      each_eid(j,i)=C_NULL_CHAR
      event_ids(i)=c_loc(each_eid(1,i))
    end do
    call edatScheduleTask_c(c_funloc(task), c_loc(task_name_processed), number_dependencies, ranks, &
      c_loc(event_ids), true_flag , true_flag)
    deallocate(each_eid, ranks, event_ids, task_name_processed)
  end subroutine edatSchedulePersistentNamedGreedyTask

  subroutine edatFireEvent_nodata(data_type, data_count, target_rank, event_id)
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(C_NULL_PTR, data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_nodata

  subroutine edatFireEvent_character_array(user_data, data_type, data_count, target_rank, event_id)
    character, dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_character_array

  subroutine edatFireEvent_integer_array(user_data, data_type, data_count, target_rank, event_id)
    integer, dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_integer_array

  subroutine edatFireEvent_long_array(user_data, data_type, data_count, target_rank, event_id)
    integer(kind=8), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_long_array

  subroutine edatFireEvent_float_array(user_data, data_type, data_count, target_rank, event_id)
    real(kind=4), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_float_array

  subroutine edatFireEvent_double_array(user_data, data_type, data_count, target_rank, event_id)
    real(kind=8), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_double_array

  subroutine edatFireEvent_integer(user_data, data_type, data_count, target_rank, event_id)
    integer, target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_integer

  subroutine edatFireEvent_long(user_data, data_type, data_count, target_rank, event_id)
    integer(kind=8), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_long

  subroutine edatFireEvent_float(user_data, data_type, data_count, target_rank, event_id)
    real(kind=4), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_float

  subroutine edatFireEvent_double(user_data, data_type, data_count, target_rank, event_id)
    real(kind=8), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFireEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFireEvent_double

  subroutine edatFirePersistentEvent_character_array(user_data, data_type, data_count, target_rank, event_id)
    character, dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_character_array

  subroutine edatFirePersistentEvent_integer_array(user_data, data_type, data_count, target_rank, event_id)
    integer, dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_integer_array

  subroutine edatFirePersistentEvent_long_array(user_data, data_type, data_count, target_rank, event_id)
    integer(kind=8), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_long_array

  subroutine edatFirePersistentEvent_float_array(user_data, data_type, data_count, target_rank, event_id)
    real(kind=4), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_float_array

  subroutine edatFirePersistentEvent_double_array(user_data, data_type, data_count, target_rank, event_id)
    real(kind=8), dimension(:), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_double_array

  subroutine edatFirePersistentEvent_integer(user_data, data_type, data_count, target_rank, event_id)
    integer, target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_integer

  subroutine edatFirePersistentEvent_long(user_data, data_type, data_count, target_rank, event_id)
    integer(kind=8), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_long

  subroutine edatFirePersistentEvent_float(user_data, data_type, data_count, target_rank, event_id)
    real(kind=4), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_float

  subroutine edatFirePersistentEvent_double(user_data, data_type, data_count, target_rank, event_id)
    real(kind=8), target, intent(in) :: user_data
    integer, intent(in) :: data_type, data_count, target_rank
    character(len=*), intent(in) :: event_id

    character(100) :: string_value
    integer :: str_len

    string_value=trim(event_id)
    string_value=adjustl(string_value)
    str_len=len(trim(event_id))+1
    string_value(str_len:str_len)=C_NULL_CHAR

    call edatFirePersistentEvent_c(c_loc(user_data), data_type, data_count, target_rank, string_value)
  end subroutine edatFirePersistentEvent_double
end module edat
