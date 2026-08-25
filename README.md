Final assignment OS. 

! Test_vars is already in the code, so do NOT convert it from the terminal !
! Only test_loop, looptest, test_fork, test_while (And perhaps other examples) should be CONVERTED from the TERMINAL !

 # Converting WINDOWS #
 - Navigate to the Arduino assignment map. 
 - Navigate to the convert map. 
 - .\convert.exe test_while [ correct ARDUINO COM ']

# Checkpoints #: 
- FREESPACE 
- ADD files (incl. text)
- RETRIEVE files
- ERASE files
- FILES -> Check all the files in the EEPROM

# Process (Run both processes) # 
- Convert:  'looptest' & 'test_loop' from the terminal ( converter map)
- RUN both processes:
     - RUN process 1 & RUN process 2
     - SUSPEND one process (use the correct PID number)
     - RESUME the same process (use correct PID number)
     - KILL a process (use correct PID number)
     - use LIST command to check the status of the processes:
                              - R -> RUNNING
                              - P -> paused
                              - NO process found in list -> Killed
# Process (Check status)
- Convert either 'looptest' or 'test_loop' -> If it's already converted, do not convert again.
- RUN the process
- use LIST to see the status of the process
- use -> SUSPEND to pause the process (use LIST command to see the status -> P)
- use -> RESUME to resume the process (use LIST command to see the status -> R)
- use -> KILL to kill/end the process (use LIST command to see no process in the list)
  
# Test_vars # 
- Test_vars is HARDCODED, so do not convert it from the terminal.
- use -> RUN test_vars.

# Forking # 
- Convert BOTH 'test_fork' & 'test_while' from the terminal.
- use RUN test_fork, to start first process.
- After INT 5, the second process starts.


  
