#include <Arduino.h>
#include <string.h>
#include <EEPROM.h>
#include "instruction_set.h"


// General system constants
#define BUFSIZE 12
#define MAX_FILES 10 //Max. amount of files that can be STORED
#define FILENAME_SIZE 12 //Max. amount of length for the FILENAMES

//FAT. FIles are stored in EEPROM. 
//FAT keeps track where the files are located.
struct FATEntry
{
  char filename[FILENAME_SIZE]; //FILENAMES
  int start; //STARTADRESS of the files
  int length; //LEngth of the files (in bytes)
};

// Memory and process limits
#define MEMSIZE 256
#define STACKSIZE 32
#define MAX_VARS 15 //Max. amount of variables.
#define MAX_PROCESSES 4 //Max. amount of processes that can be run

//Defines the states of the processes. 
#define RUNNING 'r' //Process is exectued and RUNNING
#define PAUSED  'p' //Process is executed but is PAUSED.
#define TERMINATED 0 //PRocess is FINISHED or KILLED.

// Supported variable types.
enum VarType
{
  CHAR_TYPE = 1, //Character value is 1 byte
  INT_TYPE = 2, //Integer value is 2 bytes
  FLOAT_TYPE = 4, //Floating point value is 4 bytes
  STRING_TYPE = 5
}; 

// Memory table entry
struct MemoryEntry      //Gives overview of the name, type and size of the variable. the location of the variable and the process number. 
{
  byte name; 
  byte type;
  byte address;
  byte size;
  int processId;
};

//PROCESS layout
//Every running file(program) is represented as a process.
struct Process //COntains all the information of every process
{
  char filename[FILENAME_SIZE]; //Name of the executed file

  int processId; //An unique process ID number

  char state; //The state of the process (RUNNING, PAUSED or KILLED

  // Program Counter:
  int pc;    // Points to the next instruction in EEPROM.
  
  // File Pointer:
  int fp;

  //Stack pointer
  byte sp; // Points to the next free position on the stack.

  int loopStart;  // Stores the start address of a loop.

  // Stores variables, temporary values and arguments.
  byte stack[STACKSIZE];
  };

  
//EEPROM
const int FAT_START = 0; // FAT starts at the beginning of EEPROM.
const int FAT_SIZE = sizeof(FATEntry); //Size of the FAT entry in BYTES.

const int FILECOUNT_ADDR = FAT_START + MAX_FILES * FAT_SIZE; // EEPROM address where the number of files is stored.

const int DATA_START = FILECOUNT_ADDR + 1; // Start address of the actual file data.

EERef noOfFiles = EEPROM[FILECOUNT_ADDR]; //Number of files stored in EEPROM

//MEMORY management
byte memory[MEMSIZE];// Main memory used to store variables.

MemoryEntry memoryTable[MAX_VARS]; // COntains information about all variables.

byte noOfVars = 0; // Current number of variables stored.

Process processTable[MAX_PROCESSES]; // Table contains all the active processes.

int nextProcessId = 1; // a Counter used to generate unique process ID numbers for every program(process).
int currentProcess = -1; //Index for the process that is currently executed.

//FIlE STORED in system
void writeFATEntry(int index, FATEntry &entry); //
void readFATEntry(int index, FATEntry &entry); 

int findFile(const char *name);
void sortFAT(FATEntry entries[], int count);
int findFreeSpace(int size); //Free block in the EEPROM memory

//FILES commands
void store(); //STORES file in EEPROM
void retrieve(); //RETRIEVE file information from EEPROM
void erase(); //ERASES file from EEPROM
void files(); //Shows all the STORED files in the eeprom
void freeSpace(); //Shows the current block of the freespace (in BYTES)

//PROCESS functions
void runProgram(); //Starts a process
void listProcesses(); //Shows the active processes 
void suspend(); //PAUSES executed process
void resume(); //Continue a paused process
void killProcess(); //TERMINATES a executed process
void testVars(); //Test variable system.
int findProcess(int pid); //FInd a process with the PID (process ID)
void runProcesses();// Execute one instruction of every running process.
void execute(int index); //Executes BYTE program.

//STACK function
//IMPORTANT = LIFO -> Last In First Out
char popChar(); //Pop CHAR value from the STACK
int popInt(); //pop INT value from STACK
float popFloat(); //Pop FLOATING value from STACK
char* popString(); //Pop a STREING value from the STACK

void pushChar(char c); //Pushes a char value on the STACK
void pushInt(int value); //Pushes INT value on the stack
void pushFloat(float value); // Pushes a floating value on the STACK
void pushString(const char *s); //Pushes a String value on the STACK.

void printTopValue(bool newline); // Print the value currently on top of the stack.
// If newline is true, a newline is printed afterwards.


// ======================================================
// STACK HELPER FUNCTIONS
// ======================================================
byte peekType(); //Returns DATATYPE value on top of the stack without removing it from the STACK.

byte peekType()
{
  Process &p =
    processTable[currentProcess];

  return p.stack[p.sp - 1];
}

// Changes the state of a process.
// Possible states are RUNNING, PAUSED and TERMINATED.
void changeProcessState(
  int index,
  char newState
);

// Prints the value on top of the stack.
void printTopValue(bool newline)
{
  byte type =
    peekType();

  if (type == CHAR_TYPE)
  {
    char v = popChar();

    if (newline)
      Serial.println(v);
    else
      Serial.print(v);
  }

  else if (type == INT_TYPE)
  {
    int v = popInt();

    if (newline)
      Serial.println(v);
    else
      Serial.print(v);
  }

  else if (type == FLOAT_TYPE)
  {
    float v = popFloat();

    if (newline)
      Serial.println(v);
    else
      Serial.print(v);
  }

  else if (type == STRING_TYPE)
  {
    char *v = popString();

    if (newline)
      Serial.println(v);
    else
      Serial.print(v);
  }
}


void sortMemoryTable();
byte findMemorySpace(byte size); // Searches for a free block of memory.

// Finds a variable using its name and process ID.
// Returns the table index or -1 if not found.
int findVariable(byte name, int processId);

// Stores a value from the stack into memory.
void storeVar(byte name, int processId);

// Loads a variable from memory onto the stack.
void loadVar(byte name, int processId);

// Removes all variables belonging to a process.
void deleteVariables(int processId);

// Searches the process table for a process ID.
// Returns the index of the process or -1 if not found.
int findProcess(int pid)
{
  for (int i = 0;
       i < MAX_PROCESSES;
       i++)
  {
    if (processTable[i].state != TERMINATED &&
        processTable[i].processId == pid)
    {
      return i;
    }
  }

  return -1;
}

// Changes the execution state of a process.
void changeProcessState(
  int index,
  char newState
)
{
  if (processTable[index].state == newState)
  {
    Serial.println(
      "ERROR: already in that state"
    );
    return;
  }

  processTable[index].state = newState;

  Serial.print("Process ");
  Serial.print(processTable[index].processId);
  Serial.print(" state = ");
  Serial.println(newState);
}

// Maps a command name to the function that executes it.
// Used by the command line interface.
typedef struct
{
  char name[BUFSIZE];
  void (*func)();
} commandType;

// ======================================================
// PROCESS SCHEDULER STUB
// ======================================================


// Executes one instruction for every process
// that is currently in the RUNNING state.
void runProcesses()
{
  for (int i = 0;
       i < MAX_PROCESSES;
       i++)
  {
    if (processTable[i].state ==
        RUNNING)
    {
      execute(i);
    }
  }
}
// ======================================================
// PARSER STATE MACHINE
// ======================================================


// State machine used to process multi-word commands.
// Determines which argument is currently expected.
enum ParserState
{
  WAIT_COMMAND, 

  STORE_FILENAME,
  STORE_SIZE,

  RETRIEVE_FILENAME,
  ERASE_FILENAME,
  RUN_FILENAME,

  SUSPEND_ID,
  RESUME_ID,
  KILL_ID
};

ParserState state = WAIT_COMMAND; // Current state of the command line parser.


char filename[BUFSIZE]; // Temporary storage for command arguments.
char sizeStr[BUFSIZE]; // Stores file size argument for STORE.
char idStr[BUFSIZE]; // Stores process ID argument for process commands.


// Tests storing and loading of all supported
// variable types (INT, FLOAT, CHAR and STRING).
void testVars()
{
  Serial.println("=== TEST VARS ===");

  // INT
  pushInt(123);
  storeVar('A', 0);

  loadVar('A', 0);

  Serial.print("A = ");
  Serial.println(popInt());

  // FLOAT
  pushFloat(3.14);
  storeVar('B', 0);

  loadVar('B', 0);

  Serial.print("B = ");
  Serial.println(popFloat());

  // CHAR
  pushChar('Z');
  storeVar('C', 0);

  loadVar('C', 0);

  Serial.print("C = ");
  Serial.println(popChar());

  // STRING
  pushString("hello");
  storeVar('D', 0);

  loadVar('D', 0);

  Serial.print("D = ");
  Serial.println(popString());

  Serial.println("=== END TEST ===");
  pushInt(100);
  storeVar('X', 0);

  loadVar('X', 0);
  Serial.print("X = ");
  Serial.println(popInt());

  pushInt(200);
  storeVar('X', 0);

  loadVar('X', 0);
  Serial.print("X = ");
  Serial.println(popInt());
}

// Creates a new file in EEPROM.
void store()
{
  int size = atoi(sizeStr);

  if (size <= 0)
  {
    Serial.println("ERROR: invalid size"); // 1. Validates the file size.
    return;
  }

  if (noOfFiles >= MAX_FILES)
  {
    Serial.println("ERROR: FAT full"); // 2. Checks if the FAT has space.
    return;
  }

  if (findFile(filename) != -1)
  {
    Serial.println("ERROR: file exists"); // 3. Checks if the filename already exists.
    return;
  }

  int startPos = findFreeSpace(size);

  if (startPos == -1)
  {
    Serial.println("ERROR: no space"); // 4. Finds free EEPROM space.
    return;
  }

  if (startPos + size > EEPROM.length())
  {
    Serial.println("ERROR: exceeds EEPROM");
    return;
  }

  FATEntry entry; // 5. Creates a FAT entry.

  strncpy(entry.filename, filename, FILENAME_SIZE - 1);
  entry.filename[FILENAME_SIZE - 1] = '\0';
  entry.start = startPos;
  entry.length = size;

  writeFATEntry(noOfFiles, entry);

  noOfFiles++; // 6. Stores file data received via Serial.

  for (int i = 0; i < size; i++)
  {
    while (!Serial.available())
    {
      runProcesses();
    }

    byte b = Serial.read();

    EEPROM.write(startPos + i, b);
  }

  while (Serial.available())
  {
    Serial.read();
  }

  Serial.println("File stored");
}

//Retrieves information from a file.
void retrieve()
{
  int index = findFile(filename);

  if (index == -1)
  {
    Serial.println("ERROR: file not found"); //Searches for FILE
    return;
  }

  FATEntry entry;

  readFATEntry(index, entry); // Reads a file from EEPROM and sends its

  for (int i = 0; i < entry.length; i++)
  {
    Serial.write(
      EEPROM.read(entry.start + i)
    );
  }

  Serial.println(); //Send content of the FILE to the Serial Monitor.
}

// Removes a file from the FAT.
void erase()
{
  int index = findFile(filename); 

  if (index == -1)
  {
    Serial.println("ERROR: file not found"); //Searches for FILE
    return;
  }

  FATEntry temp;

  for (int i = index; i < noOfFiles - 1; i++)
  {
    readFATEntry(i + 1, temp);
    writeFATEntry(i, temp); 
  }

  noOfFiles--; //Removes FILE from the FAT

  FATEntry empty = {"", 0, 0};

  writeFATEntry(noOfFiles, empty);

  Serial.println("File erased"); //Gives output that File has been deleted.
}

//Shows current STORED FILES.
void files()
{
  FATEntry entry;

  Serial.println("FILES:");

  for (int i = 0; i < noOfFiles; i++) // Displays all files currently stored 
  {
    readFATEntry(i, entry);

    Serial.print(entry.filename);

    Serial.print(" ");

    Serial.print(entry.length);
 
    Serial.println(" bytes"); //Displays the current bytes of the files. 
  }
}

// Calculates the size of the largest
// available free block in EEPROM.
void freeSpace()
{
  FATEntry files[MAX_FILES];

  for (int i = 0; i < noOfFiles; i++)
  {
    readFATEntry(i, files[i]);
  }

  sortFAT(files, noOfFiles);

  int largest = 0;

  int current = DATA_START;

  for (int i = 0; i < noOfFiles; i++)
  {
    int gap =
      files[i].start - current;

    if (gap > largest)
    {
      largest = gap;
    }

    current =
      files[i].start +
      files[i].length;
  }

  int gap =
    EEPROM.length() - current;

  if (gap > largest)
  {
    largest = gap; //Gives the largest free block of space 
  }

  Serial.print("Largest free block: ");

  Serial.print(largest);

  Serial.println(" bytes");
}

// Creates a new process from a stored file.
void runProgram()
{
  int fileIndex =
    findFile(filename); //Searches for the file

  if (fileIndex == -1)
  {
    Serial.println(
      "ERROR: file not found" //FIle doesnt exist.
    );

    return;
  }

//Look for a free space in the table
  int slot = -1;
  for (int i = 0;
       i < MAX_PROCESSES;
       i++)
  {
    if (processTable[i].state ==
        TERMINATED)
    {
      slot = i;
      break;
    }
  }

//If process table is full, a new process can not be started.
  if (slot == -1)
  {
    Serial.println(
      "ERROR: process table full"
    );

    return;
  }

//Read information from the file stored in the FAT
  FATEntry file;

  readFATEntry(
    fileIndex,
    file
  );

  Process &p =
    processTable[slot];
  currentProcess = slot;

//Store the filename with the chose processor.
  strcpy(
    p.filename,
    file.filename
  );

// Geef het proces een uniek process ID.
  p.processId =
    nextProcessId++;

  p.state = RUNNING; //Put process state in RUNNING -> Proces is executed and runs.
  p.pc = file.start; //POints to the beginning of the program

  p.fp = 0;
  p.sp = 0; //When it starts, the SP is 0
  p.loopStart = file.start; //Remembers the beginning of a loop
  for (int i = 0; i < STACKSIZE; i++)
  {
    p.stack[i] = 0;
  }

  Serial.print(
    "Process started. PID=" //Shows the ID number of a process in the SERIAL MONITOR. 
  );

  Serial.println(
    p.processId
  );
}

//Displays all the executed processes 
void listProcesses()
{
  Serial.println(
    "===== PROCESS LIST ====="
  );

  for (int i = 0;
       i < MAX_PROCESSES;
       i++) 
  {
    if (processTable[i].state != TERMINATED) //Does NOT show the terminated state of the PROCESSES
    {
      Serial.print("PID: ");
      Serial.print(processTable[i].processId); //Gives process number

      Serial.print(" State: ");
      Serial.print(processTable[i].state); //State of Process -> RUNNING or PAUSED

      Serial.print(" File: ");
      Serial.print(processTable[i].filename); //Gives the filename of the corresponding process

      Serial.print(" PC:");
      Serial.print(processTable[i].pc); //Gives the Program COunter

      Serial.print(" SP:");
      Serial.print(processTable[i].sp); //Gives the stack pointer

      Serial.print(" FP:");
      Serial.print(processTable[i].fp); //Gives the FIle pointer

      Serial.println();
    }
  }
}

//Pauses the process
void suspend()
{
  int pid = atoi(idStr); //COnverts STRING input from the user (From RUNNING to PAUSE) to an INT of the PID.

  int index =
    findProcess(pid); //Searches through the table for the PID index

  if (index == -1)
  {
    Serial.println(
      "ERROR: process not found" //Gives an error text if the process does not exist.
    );

    return;
  }

  changeProcessState(
    index,
    PAUSED //IF the PID does exist, change the state from RUNNING to PAUSED
  );
}

//RESUME the state of a process from PAUSED to RUNNING
void resume()
{
  int pid = atoi(idStr); //Convert STRING input from user to INT of the corresponding process

  int index =
    findProcess(pid); //Searches through the table for the PID

  if (index == -1)
  {
    Serial.println(
      "ERROR: process not found" //Gives an error text if the process does not exist
    );

    return;
  }

  changeProcessState(
    index,
    RUNNING //If the process DOES exist, change the state from Paused to RUNNING again
  );
}

//TERMINATE a process
void killProcess()
{
  int pid = atoi(idStr); //Convert STRING input from user to INT of the corresponding process

  int index =
    findProcess(pid); //Searches through the table for the process

  if (index == -1)
  {
    Serial.println(
      "ERROR: process not found" //Gives an error text if the process does not exist
    );

    return;
  }

  deleteVariables(pid);
  if (currentProcess == index)
  {
    currentProcess = 0; //Deletes the variables from this state
  }
  processTable[index].state =
    TERMINATED; //State of the process changes from RUNNING to TERMINATED (KILL)

  //The pointers of the process are resetted to 0.
  processTable[index].sp = 0;
  processTable[index].fp = 0;
  processTable[index].pc = 0;

  Serial.print(
    "Process killed: "
  ); //Gives output text where the process is terminated. 

  Serial.println(pid);
}

//Pushes byte of the runnning process to the stack
void pushByte(byte b)
{
  Process &p =
    processTable[currentProcess]; //Reference to the current RUNNING process

  p.stack[p.sp++] = b; // Store the byte at the current stack positio
}
// Removes and returns the top byte from the stack.
byte popByte()

{
  Process &p =
    processTable[currentProcess];

  return p.stack[--p.sp]; // Decrement the stack pointer and return
}

//Stores characte on the stack
void pushChar(char c)
{
  pushByte((byte)c); //DAta value
  pushByte(CHAR_TYPE); //Character type
}


char popChar()
{
  popByte();
  return (char)popByte();//Retrieves character from the stack
}

//Stores INT datavalue on the stack
void pushInt(int value)
{
  pushByte(highByte(value)); //Pushes highbyte of the INT on the stack
  pushByte(lowByte(value)); //Pushes lowbyte of the INT on the stack
  pushByte(INT_TYPE); //Geef datatype INT
}

//INT datavalues might be too big, so they get stored in a lowbyte and a highbyte. these values are stored seperately on the STACK, the INT_TYPE gives the VALUE type. 
//ON the STACK it takes the latest on the top and does the calculations with it. 

int popInt() //Popped values from a STACK
{
  popByte(); // Pops the datatype from the STACK

  byte low = popByte(); //Pops the first value of the STACK
  byte high = popByte();

  return word(high, low); //Combine these bytes to a whole INTEGER 
}


//Pushes float value on the stack
void pushFloat(float value)
{
  byte b[4]; //Arduino FLOAT uses 4 BYTES

  float *pf = (float *)b; 

  *pf = value; // A byte array is stored as a FLOAT

  for (int i = 0; i < 4; i++) //So every byte of a float (till 4) is pushed on the stack.
  {
    pushByte(b[i]);
  }

  pushByte(FLOAT_TYPE); //Pushes the DATA_TYPE
}


float popFloat() // POps a FLOAT value from the stack
{
  popByte(); // POps the Data_type from the stack (First)

  byte b[4];

  for (int i = 3; i >= 0; i--) //Makes sure the order of the floats is the same as it was pushed. 
  {
    b[i] = popByte();
  }

  float *pf = (float *)b;

  return *pf; //The array is intepreted as a FLOAT
}

//Pushes STRING on the stack
void pushString(const char *s)
{
  byte len = strlen(s) + 1; //Calculates the length of the string (includes \0)
 
  for (int i = 0; i < len; i++) //Pushes characters one by one on the stack.
  {
    pushByte(s[i]);
  }

  pushByte(len);

  pushByte(STRING_TYPE);
}

//A pointer to the string on the STACK
char* popString()
{
  Process &p =
    processTable[currentProcess]; 

  popByte();

  byte len = popByte(); //Gives the length of the STRING 
  p.sp -= len; //Pointer is set to the beginning of the length of the string
  return (char*)&p.stack[p.sp]; //Gives the adress of the beginning of the String. Output is the complete string (text).
}

//pops the value from a stack without knowing which data_type it is. 
float popVal()
{
  Process &p =
    processTable[currentProcess]; 

  byte type =
    p.stack[p.sp - 1]; //Checks what data_type it is (CHAR, INT or FLOAT

  if (type == CHAR_TYPE) //Returns specific type of data.
  {
    return popChar();
  }

  if (type == INT_TYPE)
  {
    return popInt();
  }

  if (type == FLOAT_TYPE)
  {
    return popFloat();
  }

  return 0;
}

//Looks at the value on the stack, without removing it. 
float peekVal()
{ Process &p =
    processTable[currentProcess];

  byte oldSp = p.sp;

  float value = popVal(); //Reads value on the stack. 

  p.sp = oldSp; //Restores the Stack Pointer

  return value;
}

//Searches for a variable in the memory table
int findVariable(byte name, int processId)
{
  for (int i = 0; i < noOfVars; i++)
  {
    if (memoryTable[i].name == name &&
        memoryTable[i].processId == processId)  //Different processes can have the same variable name. 
    {
      return i;
    }
  }

  return -1; //Either returns the index of the variable or nothing if it does not exist. 
}

//Stores the value on top of the stack as a variable
void storeVar(byte name, int processId)
{
  Process &p =
    processTable[currentProcess]; //Retrieves corresponding process, variables are connected with a process.

  byte type =
    p.stack[p.sp - 1]; //Reads the datatype of the STACK.

  MemoryEntry entry; 
  //Creates a new explanation of the variables

  entry.name = name; //NAme variable
  entry.type = type; //Datatype
  entry.processId = processId; //Corresponding Process

//Depending on the datatype, the size is determined (BYTES)
  if (type == CHAR_TYPE)
    entry.size = 1;
  else if (type == INT_TYPE)
    entry.size = 2;
  else if (type == FLOAT_TYPE)
    entry.size = 4;
  else
    entry.size = p.stack[p.sp - 2];

//Checks if variable already exists
  int oldIndex =
    findVariable(name, processId);
  if (oldIndex == -1 && 
      noOfVars >= MAX_VARS) 
  {
    Serial.println(
      "ERROR: variable table full" //if variable does not exists, but table is full, it gives an error
    );
    return;
  }

  if (oldIndex != -1) //If variable already exists
  {
    entry.address =
      memoryTable[oldIndex].address; //It takes the adress of the old variable for the new one. 

    for (int i = oldIndex; // deletes the old table of the previous variable. 
         i < noOfVars - 1;
         i++)
    {
      memoryTable[i] =
        memoryTable[i + 1];
    }

    noOfVars--;
  }
  else
  {
    entry.address = //if it is a new variable, searches for free space. 
      findMemorySpace(entry.size);

    if (entry.address == 255)
    {
      Serial.println(
        "ERROR: memory full"
      );
      return;
    }
  }

  memoryTable[noOfVars++] = entry; //Adds the new variable. 

  if (type == STRING_TYPE)
  {
    popByte(); // type
    popByte(); // lengte
  }
  else
  {
    popByte(); // type
  }

  for (int i = entry.size - 1; i >= 0; i--) //Copies the value to the memory. Since it is STACK it is LIFO. 
  {
    memory[entry.address + i] = //Copies from STACK to Memory. 
      popByte();
  }
}

//Loads variable from memory back on stack
void loadVar(byte name, int processId)
{
  int index =
    findVariable(name, processId); //Searches for a variable. 

  if (index == -1)
  {
    Serial.println("Variable not found"); //If variable does not exist. 
    return;
  }

  MemoryEntry &entry = //Retrieves all the information of the variable 
    memoryTable[index];

  for (int i = 0; i < entry.size; i++) //Copies it to the stack
  {
    pushByte(
      memory[entry.address + i]
    );
  }

  if (entry.type == STRING_TYPE)
  {
    pushByte(entry.size);
  }

  pushByte(entry.type);
}

//Sorts the variable on the memory
void sortMemoryTable()
{
  for (int i = 0; i < noOfVars - 1; i++) 
  {
    for (int j = i + 1; j < noOfVars; j++)
    {
      if (memoryTable[j].address <
          memoryTable[i].address)
      {
        MemoryEntry temp =
          memoryTable[i];

        memoryTable[i] =
          memoryTable[j];

        memoryTable[j] =
          temp;
      }
    }
  }
}
byte findMemorySpace(byte size) //Searches for freespace. 
{
  sortMemoryTable(); //First sorts the variables in the memory based on size. 

  byte current = 0; //Start of the memory

  for (int i = 0; i < noOfVars; i++) //If the size of the gap is big enough, gives back startadress.
  {
    if (memoryTable[i].address -
        current >= size)
    {
      return current;
    }

    current =
      memoryTable[i].address + 
      memoryTable[i].size;
  }

  if (MEMSIZE - current >= size) // checks at the end if there is space after the variables.
  {
    return current;
  }

  return 255; //if there is no space, it returns the 255. 
}

//Deletes the variables of a process. 
void deleteVariables(int processId)
{
  for (int i = 0; i < noOfVars;)
  {
    if (memoryTable[i].processId ==
        processId) //Variables with the corresponding process. 
    {
      for (int j = i;
           j < noOfVars - 1;
           j++) 
      {
        memoryTable[j] =
          memoryTable[j + 1];
      }

      noOfVars--; //When the variable is one less, it shifts the entries 
    }
    else
    {
      i++;
    }
  }
}

//Command table.
//It calls the representive functions. 
static commandType command[] =
{
  {"STORE",     &store},
  {"RETRIEVE",  &retrieve},
  {"ERASE",     &erase},
  {"FILES",     &files},
  {"FREESPACE", &freeSpace},
  {"RUN",       &runProgram},
  {"LIST",      &listProcesses},
  {"SUSPEND",   &suspend},
  {"RESUME",    &resume},
  {"KILL",      &killProcess},
  {"TESTVARS",  &testVars}
};

static int nCommands =
  sizeof(command) / sizeof(commandType);

int findCommand(const char *name) //Searches for the commands in the command table. 
{
  for (int i = 0; i < nCommands; i++)
  {
    if (strcmp(name, command[i].name) == 0)
    {
      return i;
    }
  }

  return -1;
}

// HELP
void printHelp() //If a wrong command is written by the user, and Error text will be shown including the available commands. 
{
  Serial.println("Unknown command");
  Serial.println("Available commands:");

  for (int i = 0; i < nCommands; i++)
  {
    Serial.println(command[i].name);
  }
}

// While the OS is running proccesses, user should be able to write. 
//Every test before the [space] is a individual piece, so a different 'token'
//Example: RUN Hello -> two tokens (1= RUN, 2=hello)
bool readToken(char token[])
{
  static char buffer[BUFSIZE];
  static int pos = 0;

  while (Serial.available())
  {
    char c = Serial.read();

    if (c == ' ' || c == '\n' || c == '\r')
    {
      if (pos > 0)
      {
        buffer[pos] = '\0';

        strncpy(token, buffer, BUFSIZE - 1);
        token[BUFSIZE - 1] = '\0'; //It reads every character, and after the space, the 'word' is known

        pos = 0;
        buffer[0] = '\0';

        return true;
      }
    }
    else
    {
      if (pos < BUFSIZE - 1)
      {
        buffer[pos++] = c;
      }
    }
  }

  return false;
}

//Makes sure that the command is being read, and that the function of the file is executed. 
//Example: RUN hello -> RUN is th ecommand being read, and the hello file is being executed by the runprogram() 
void processToken(char token[])
{
  switch (state)
  {
    case WAIT_COMMAND:
      {
        int cmd = findCommand(token);

        if (cmd == -1)
        {
          printHelp();
          break;
        }

        if (strcmp(token, "STORE") == 0)
        {
          state = STORE_FILENAME;
        }

        else if (strcmp(token, "RETRIEVE") == 0)
        {
          state = RETRIEVE_FILENAME;
        }

        else if (strcmp(token, "ERASE") == 0)
        {
          state = ERASE_FILENAME;
        }

        else if (strcmp(token, "RUN") == 0)
        {
          state = RUN_FILENAME;
        }

        else if (strcmp(token, "SUSPEND") == 0)
        {
          state = SUSPEND_ID;
        }

        else if (strcmp(token, "RESUME") == 0)
        {
          state = RESUME_ID;
        }

        else if (strcmp(token, "KILL") == 0)
        {
          state = KILL_ID;
        }

        else
        {
          command[cmd].func();
        }

        break;
      }

    case STORE_FILENAME:

      strncpy(filename, token, BUFSIZE - 1);
      filename[BUFSIZE - 1] = '\0';
      state = STORE_SIZE;

      break;

    case STORE_SIZE:
      strncpy(sizeStr, token, BUFSIZE - 1);
      sizeStr[BUFSIZE - 1] = '\0';
      store();

      state = WAIT_COMMAND;

      break;

    case RETRIEVE_FILENAME:

      strncpy(filename, token, BUFSIZE - 1);
      filename[BUFSIZE - 1] = '\0';

      retrieve();

      state = WAIT_COMMAND;

      break;

    case ERASE_FILENAME:

      strncpy(filename, token, BUFSIZE - 1);
      filename[BUFSIZE - 1] = '\0';

      erase();

      state = WAIT_COMMAND;

      break;

    case RUN_FILENAME:

      strncpy(filename, token, BUFSIZE - 1);
      filename[BUFSIZE - 1] = '\0';

      runProgram();

      state = WAIT_COMMAND;

      break;

    case SUSPEND_ID:

      strncpy(idStr, token, BUFSIZE - 1);
      idStr[BUFSIZE - 1] = '\0';

      suspend();

      state = WAIT_COMMAND;

      break;

    case RESUME_ID:

      strncpy(idStr, token, BUFSIZE - 1);
      idStr[BUFSIZE - 1] = '\0';

      resume();

      state = WAIT_COMMAND;

      break;

    case KILL_ID:

      strncpy(idStr, token, BUFSIZE - 1);
      idStr[BUFSIZE - 1] = '\0';

      killProcess();

      state = WAIT_COMMAND;

      break;
  }
}

char token[BUFSIZE];

//Example hello byte program
void installHello()
{
    if (findFile("hello") != -1)
        return; //Finds the file in the EEPROM

    FATEntry entry;

    strcpy(entry.filename, "hello");

    entry.start = 400;

    int addr = entry.start;

    //CREATES the bytecode in EEPROM
    EEPROM.write(addr++, STRING);

    EEPROM.write(addr++, 'H');
    EEPROM.write(addr++, 'e');
    EEPROM.write(addr++, 'l');
    EEPROM.write(addr++, 'l');
    EEPROM.write(addr++, 'o');
    EEPROM.write(addr++, ',');
    EEPROM.write(addr++, ' ');
    EEPROM.write(addr++, 'w');
    EEPROM.write(addr++, 'o');
    EEPROM.write(addr++, 'r');
    EEPROM.write(addr++, 'l');
    EEPROM.write(addr++, 'd');
    EEPROM.write(addr++, '.');
    EEPROM.write(addr++, '\0');

    EEPROM.write(addr++, PRINTLN);

    EEPROM.write(addr++, STOP);

    entry.length =
        addr - entry.start;

    writeFATEntry(
        noOfFiles,
        entry
    );

    noOfFiles++;
}

//Another hardcoded Looptest, 
//Example loop, so the process state can be checked. 
void installLoopTest()
{
    if(findFile("looptest") != -1)
        return;

    FATEntry entry;

    strcpy(entry.filename, "looptest");

    entry.start = 700;

    int addr = entry.start;

    // INT 0
    EEPROM.write(addr++, INT);
    EEPROM.write(addr++, 0);
    EEPROM.write(addr++, 0);

    // PRINTLN
    EEPROM.write(addr++, PRINTLN);

    // Geen STOP !!!

    entry.length =
        addr - entry.start;

    writeFATEntry(
        noOfFiles,
        entry
    );

    noOfFiles++;
}

//Example test_vars hardcoded byteprogram. 
//Can be RUN with this hardcoded, or the converter. 
void installTestVars()
{
    if (findFile("test_vars") != -1)
        return;

    FATEntry entry;

    strcpy(entry.filename, "test_vars");

    entry.start = 500;

    int addr = entry.start;

    // "passed" SET s

    EEPROM.write(addr++, STRING);

    EEPROM.write(addr++, 'p');
    EEPROM.write(addr++, 'a');
    EEPROM.write(addr++, 's');
    EEPROM.write(addr++, 's');
    EEPROM.write(addr++, 'e');
    EEPROM.write(addr++, 'd');
    EEPROM.write(addr++, '\0');

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 's');

    // 'a' SET c

    EEPROM.write(addr++, CHAR);
    EEPROM.write(addr++, 'a');

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'c');

    // 263 SET i

    EEPROM.write(addr++, INT);

    EEPROM.write(addr++, highByte(263));
    EEPROM.write(addr++, lowByte(263));

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'i');

    // 123.45 SET f

    EEPROM.write(addr++, FLOAT);

    union
    {
        float f;
        byte b[4];
    } fl;

    fl.f = 123.45;

    for (int i = 0; i < 4; i++)
    {
        EEPROM.write(addr++, fl.b[i]);
    }

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'f');

    // GET c

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'c');

    EEPROM.write(addr++, INCREMENT);

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'c');

    // GET i

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'i');

    EEPROM.write(addr++, DECREMENT);

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'i');

    // GET f

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'f');

    EEPROM.write(addr++, INCREMENT);

    EEPROM.write(addr++, SET);
    EEPROM.write(addr++, 'f');

    // GET s PRINTLN

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 's');

    EEPROM.write(addr++, PRINTLN);

    // GET c PRINTLN

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'c');

    EEPROM.write(addr++, PRINTLN);

    // GET i PRINTLN

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'i');

    EEPROM.write(addr++, PRINTLN);

    // GET f PRINTLN

    EEPROM.write(addr++, GET);
    EEPROM.write(addr++, 'f');

    EEPROM.write(addr++, PRINTLN);

    EEPROM.write(addr++, STOP);

    entry.length =
        addr - entry.start;

    writeFATEntry(
        noOfFiles,
        entry
    );

    noOfFiles++;
}

//Starts communication 
void setup()
{
  Serial.begin(9600);

  while (!Serial);

  Serial.setTimeout(-1);

  if (noOfFiles > MAX_FILES)
  {
    noOfFiles = 0; 
  }

  for (int i = 0; i < MAX_PROCESSES; i++)
  {
    processTable[i].state = TERMINATED; //All process states are emtpy (So new files get the process ID from the beginning, and no process is already running)
  }

//Files of the example codes are being installed and STORED as the byte code file. 
    installHello();
    installTestVars();
    installLoopTest();

//Print statements.
  Serial.println("ArduinOS 1.0 ready");
  Serial.println();

  Serial.println("Available commands:");
  Serial.println("STORE");
  Serial.println("RETRIEVE");
  Serial.println("ERASE");
  Serial.println("FILES");
  Serial.println("FREESPACE");
  Serial.println("RUN");
  Serial.println("LIST");
  Serial.println("SUSPEND");
  Serial.println("RESUME");
  Serial.println("KILL");
  Serial.println();
}

//Saves FAT Entry
void writeFATEntry(int index, FATEntry &entry)
{
  EEPROM.put(FAT_START + index * FAT_SIZE, entry);
}

//Reads the FAT Entry
void readFATEntry(int index, FATEntry &entry)
{
  EEPROM.get(FAT_START + index * FAT_SIZE, entry);
}

//It searches for the file when a command is being used. 
int findFile(const char *name)
{
  FATEntry entry;

  for (int i = 0; i < noOfFiles; i++)
  {
    readFATEntry(i, entry);

    if (strcmp(entry.filename, name) == 0)
    {
      return i;
    }
  }

  return -1;
}

//Sorts the FILES on the EEPROM.
//This makes searchign for FREESPACE, easier. 
void sortFAT(FATEntry entries[], int count)
{
  for (int i = 0; i < count - 1; i++)
  {
    for (int j = i + 1; j < count; j++)
    {
      if (entries[j].start < entries[i].start)
      {
        FATEntry temp = entries[i];
        entries[i] = entries[j];
        entries[j] = temp;
      }
    }
  }
}

//Searches for the freespace block in between the files. 
int findFreeSpace(int size)
{
  FATEntry files[MAX_FILES];

  for (int i = 0; i < noOfFiles; i++)
  {
    readFATEntry(i, files[i]);
  }

  sortFAT(files, noOfFiles);

  int current = DATA_START;

  for (int i = 0; i < noOfFiles; i++)
  {
    if (files[i].start - current >= size)
    {
      return current;
    }

    current = files[i].start + files[i].length;
  }

  if (EEPROM.length() - current >= size)
  {
    return current;
  }

  return -1;
}


//Execute
//Reads the byte instructions. 
//So the byte program and the data being pushed and popped on and of the stack. 
void execute(int index)
{
  Process &p =
    processTable[index];

  currentProcess = index;

int instructionPc = p.pc;

byte instruction =
    EEPROM.read(p.pc++);

  switch (instruction)
  {
    case CHAR:
      {
        char value =
          EEPROM.read(p.pc++);

        pushChar(value);
      }
      break;

    case INT:
      {
        byte high =
          EEPROM.read(p.pc++);

        byte low =
          EEPROM.read(p.pc++);

        pushInt(
          word(high, low)
        );
      }
      break;

    case FLOAT:
      {
        byte b[4];

        for (int i = 0; i < 4; i++)
        {
          b[i] =
            EEPROM.read(p.pc++);
        }

        float *pf =
          (float*)b;

        pushFloat(*pf);
      }
      break;

    case STRING:
      {
        char buffer[32];

        int len = 0;

        while (true)
        {
          char c =
            EEPROM.read(p.pc++);

          buffer[len++] = c;

          if (c == '\0')
            break;
        }

        pushString(buffer);
      }
      break;

    case SET:
      {
        byte name =
          EEPROM.read(p.pc++);

        storeVar(
          name,
          p.processId
        );
      }
      break;

    case GET:
      {
        byte name =
          EEPROM.read(p.pc++);

        loadVar(
          name,
          p.processId
        );
      }
      break;

    case PRINT:
      {
        printTopValue(false);
      }
      break;

    case PRINTLN:
      {
        printTopValue(true);
      }
      break;

    case PLUS:
      {
        byte type2 = peekType();
        float b = popVal();

        byte type1 = peekType();
        float a = popVal();

        byte resultType =
          (type1 > type2)
          ? type1
          : type2;

        if (resultType == CHAR_TYPE)
          pushChar((char)(a + b));

        else if (resultType == INT_TYPE)
          pushInt((int)(a + b));

        else
          pushFloat(a + b);
          }
          break;
          
          case MINUS:
          {
            byte type2 = peekType();
            float b = popVal();
            byte type1 = peekType();
            float a = popVal();
            
            byte resultType =
            (type1 > type2)
            ? type1
            : type2;
            
            if (resultType == CHAR_TYPE)
            pushChar((char)(a - b));
            
            else if (resultType == INT_TYPE)
            pushInt((int)(a - b));
            
            else
            pushFloat(a - b);
            }
            break;

    case INCREMENT:
      {
        byte type = peekType();

        float a = popVal();

        if (type == CHAR_TYPE)
          pushChar((char)(a + 1));

        else if (type == INT_TYPE)
          pushInt((int)(a + 1));

        else
          pushFloat(a + 1);
      }
      break;

    case DECREMENT:
      {
        byte type = peekType();

        float a = popVal();

        if (type == CHAR_TYPE)
          pushChar((char)(a - 1));

        else if (type == INT_TYPE)
          pushInt((int)(a - 1));

        else
          pushFloat(a - 1);
      }
      break;
      
      case LOOP:
      {
    p.loopStart = p.pc;
    }
    break;
    case ENDLOOP:
    {
    p.pc = p.loopStart;
    }
    break;
    
    case IF:
    {
    byte jump =
      EEPROM.read(p.pc++);

    float value =
      peekVal();

    if (value == 0)
    {
      p.pc += jump;
      }
    }
    break;
    
    case ELSE:
    {
    byte jump =
      EEPROM.read(p.pc++);

    float value =
      peekVal();

    if (value != 0)
    {
      p.pc += jump;
      }
    }
    break;
    
    case ENDIF:
    {
    popVal();
    }
    break;
    
    case WHILE:
    {
    byte condSize =
      EEPROM.read(p.pc++);

    byte bodySize =
      EEPROM.read(p.pc++);

    float value =
      popVal();

    if (value == 0)
    {
      p.pc += bodySize + 1;
    }
    else
    {
      pushByte(
        condSize +
        bodySize +
        4
        );
      }
    }
    break;
    
    case ENDWHILE:
    {
    byte jumpBack =
      popByte();

    p.pc -= jumpBack;
    }
    break;
    
    case MILLIS:
    {
    pushInt(
      millis()
      );
    }
    break;
    
    case DELAYUNTIL:
    {
    long target =
      (long)popVal();

    if (target > millis())
    {
      pushInt(target);

      p.pc--;
      }
    }
    break;
    
    case DELAY:
    {
    long ms = (long)popVal();

    delay(ms);
    }
    break;
    
    case FORK:
    {
    char *name = popString();

    strcpy(filename, name);

    runProgram();

    pushInt(
        nextProcessId - 1
        );
    }
    break;
    case WAITUNTILDONE:
    {
    int pid = popInt();

    if(findProcess(pid) != -1)
    {
        pushInt(pid);

        p.pc = instructionPc;
        }
    }
    break;
    
case EQUALS:
{
    float b = popVal();
    float a = popVal();

    pushChar(a == b);
}
break;
case LESSTHAN:
{
    float b = popVal();
    float a = popVal();

    pushChar(a < b);
}
break;
case GREATERTHAN:
{
    float b = popVal();
    float a = popVal();

    pushChar(a > b);
}
break;
case OPEN:
{
    int offset = popInt();

    char *name = popString();

    int fileIndex =
        findFile(name);

    if(fileIndex == -1)
        break;

    FATEntry file;

    readFATEntry(fileIndex, file);

    p.fp =
        file.start + offset;
}
break;

case WRITE:
{
    byte type = peekType();

    if(type == CHAR_TYPE)
    {
        EEPROM.write(
            p.fp++,
            popChar()
        );
    }

    else if(type == INT_TYPE)
    {
        int value = popInt();

        EEPROM.write(
            p.fp++,
            highByte(value)
        );

        EEPROM.write(
            p.fp++,
            lowByte(value)
        );
    }

    else if(type == FLOAT_TYPE)
    {
        float value = popFloat();

        byte *b =
            (byte*)&value;

        for(int i=0;i<4;i++)
        {
            EEPROM.write(
                p.fp++,
                b[i]
            );
        }
    }

    else if(type == STRING_TYPE)
    {
        char *s =
            popString();

        while(true)
        {
            EEPROM.write(
                p.fp++,
                *s
            );

            if(*s == '\0')
                break;

            s++;
        }
    }
}
break;

case READCHAR:
{
    char c =
        EEPROM.read(p.fp++);

    pushChar(c);
    }
    break;

case READINT:
{
    byte high =
        EEPROM.read(p.fp++);

    byte low =
        EEPROM.read(p.fp++);

    pushInt(
        word(high, low)
    );
}
break;

case READFLOAT:
{
    byte b[4];

    for(int i=0;i<4;i++)
    {
        b[i] =
            EEPROM.read(p.fp++);
    }

    float *pf =
        (float*)b;

    pushFloat(*pf);
    }
break;

case READSTRING:
{
    char buffer[32];

    int len = 0;

    while(true)
    {
        char c =
            EEPROM.read(p.fp++);

        buffer[len++] = c;

        if(c == '\0')
            break;
    }

    pushString(buffer);
}
break;

case CLOSE:
{
    processTable[index].fp = 0;
    break;
}

    case STOP:
      {
        deleteVariables(p.processId);

        p.state = TERMINATED;

        p.sp = 0;
        p.fp = 0;
        p.pc = 0;
      }
      break;

    default:
      {
        Serial.print("Unknown instruction: ");
        Serial.println(instruction);
      }
      break;
  }
}

void loop()
{
  runProcesses(); //Runs the processes 

  if (readToken(token)) //CHecks if there is any input from the USER. 
  {
    processToken(token);
  }
}
