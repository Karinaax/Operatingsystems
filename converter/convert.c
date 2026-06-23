/* convert
 * June 2023
 * Wouter Bergmann Tiest
 * 
 * Converts a text file in bytecode-language into a binary file and uploads
 * this to an Arduino running ArduinOS using the "erase" and "store" commands.
 * 
 * Compilation with gcc or clang on Windows, Linux or MacOS:
 * gcc -o convert convert.c
 * 
 * Usage: convert <file> <serial port>
 * 
 * Interprets numbers starting with 0x as hex bytes. Characters in single quotes
 * ('x') are converted to CHAR n, with n the ASCII code. Decimal integer numbers
 * are converted to INT h l, where h and l are the high and low bytes. Strings in
 * double quotes ("text") are converted to STRING b1 b2 ... bn 0x00, with b1, b2,
 * ... the ASCII codes of the characters in the string. Numbers with a decimal
 * dot are converted to IEEE 754 32-bit floating point notation: FLOAT b1 b2 b3 b4.
 *
 * IF ... THEN ... (ELSE ...) ENDIF is converted to ... IF n1 ... (ELSE n2 ...) ENDIF,
 * with n1 the number of bytes between THEN and ELSE/ENDIF and n2 the number of
 * bytes betwen ELSE and ENDIF. WHILE ... DO ... ENDWHILE is converted to
 * ... WHILE n1 n2 ... ENDWHILE, with n1 the number of bytes between WHILE and DO,
 * and n2 the number of bytes between DO and ENDWHILE. Both constructions support
 * nesting.
 */
#define BUFSIZE 50
#define PROGSIZE 255
#define STACKSIZE 10
#define CHUNKSIZE 64
#define C_CHAR 1
#define C_INT 2
#define C_STRING 3
#define C_FLOAT 4
#define C_IF 128
#define C_ELSE 129
#define C_ENDIF 130
#define C_WHILE 131
#define C_ENDWHILE 132
#define C_THEN 138
#define C_DO 139

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "instruction_array.h"

#ifdef _WIN32
#include <windows.h>
#define BPS 9600

// Read characters from serial stream pointed to by h until timeout
// Copy characters into buffer
// Append a terminating zero
// Return number of characters read
int readAll(HANDLE h, char *buffer) {
    DWORD bytesRead = 0;
    do {
        ReadFile(h, buffer, BUFSIZE, &bytesRead, NULL);
    } while (!bytesRead);
    buffer[bytesRead] = '\0';
    return (int)bytesRead;
}

// Write a buffer to serial stream pointed to by h
// Divide up into chunks of CHUNKSIZE (64 bytes) to get around Arduino's serial buffer size
// Return number of characters written
int writeBuffer(HANDLE h, char *buffer, int noOfBytes) {
    DWORD bytesWritten;
	DWORD totalBytesWritten = 0;
	DWORD chunkSize = noOfBytes < CHUNKSIZE ? noOfBytes : CHUNKSIZE;
	while (noOfBytes > 0) {
        usleep(100000); // make sure previously written bytes have been processed
        WriteFile(h, buffer + totalBytesWritten, chunkSize, &bytesWritten, NULL);
		totalBytesWritten += bytesWritten;
		noOfBytes -= chunkSize;
	}
    return (int)totalBytesWritten;
}

// Append a newline character to buffer
// Write to serial stream pointed to by h
// Return number of characters written
int writeLine(HANDLE h, char *buffer) {
    strcat(buffer, "\n");
    return writeBuffer(h, buffer, strlen(buffer));
}
#else // Linux and MacOS
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#define BPS B9600

// Read all available characters from serial stream pointed to by h
// Copy characters into buf
// Append a terminating zero
// Return number of characters read
ssize_t readAll(int h, char *buf) {
    int bytesAvailable;
    ioctl(h, FIONREAD, &bytesAvailable);
    ssize_t bytesRead = 0, n;
    while (bytesRead < bytesAvailable) {
        n = read(h, buf, bytesAvailable - bytesRead);
        bytesRead += n;
        buf += n;
    }
    *buf = '\0';
    return bytesRead;
}

// Write buffer to serial stream pointed to by h
// Divide up into chunks of CHUNKSIZE (64 bytes) to get around Arduino's serial buffer size
// Return number of characters written
ssize_t writeBuffer(int h, unsigned char *buffer, int noOfBytes) {
    ssize_t bytesWritten = 0;
    size_t chunkSize = noOfBytes < CHUNKSIZE ? noOfBytes : CHUNKSIZE;
    while (noOfBytes > 0) {
        usleep(100000); // make sure previously written bytes have been processed
        bytesWritten += write(h, buffer + bytesWritten, chunkSize);
        noOfBytes -= chunkSize;
    }
    return bytesWritten;
}

// Append a newline character to buffer
// Write to serial stream pointed to by h
// Return number of characters written
ssize_t writeLine(int h, char *buffer) {
    strcat(buffer, "\n");
    return write(h, buffer, strlen(buffer));
}
#endif

// Return true if character is space, tab, carriage return or newline
int isWhiteSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Read a single word or multiple words within quotes from file into buf
// Return EOF if file has ended; otherwise 0
// Note: cannot deal with escaped or unbalanced quote marks
int readToken(FILE *file, char *buf) {
    // skip leading whitespace
    do {
        *buf = fgetc(file);
        if (*buf == EOF) return EOF;
    } while (isWhiteSpace(*buf));
    // start reading token
    char inQuote = 0;
    if (*buf == '\"') inQuote = 1;
    do {
        buf++;
        *buf = fgetc(file);
        if (*buf == '\"') inQuote = !inQuote;
    } while (*buf != EOF && (inQuote || !isWhiteSpace(*buf)));
    *buf = '\0'; // add terminating zero
    return 0;
}

// Convert the character after the backslash of an escaped character to the character
char unescape(char c) {
    switch (c) {
        case 'n':
            return '\n';
            break;
        case 'r':
            return '\r';
            break;
        case 't':
            return '\t';
            break;
        default: // for \\, \' and \"
            return c;
            break;
    }
}

int main(int argc, char *argv[]) {
    // check arguments
    if (argc != 3) {
        printf("Usage: %s <file> <serial port>\n", argv[0]);
        return -1;
    }

    // open input file
    FILE *file = fopen(argv[1], "r");
    if (!file) {
        printf("Cannot open file \"%s\"\n", argv[1]);
        return -1;
    }

    // process instructions
    printf("Converting file \"%s\"\n", argv[1]);
    char buf[BUFSIZE];
    unsigned char prog[PROGSIZE];
    int pc = 0;
    int *if_stack = malloc(STACKSIZE * sizeof(int));
    while (readToken(file, buf) != EOF) {
        int command = 0;
        if (*buf =='\'') { // char
            prog[pc++] = C_CHAR;
            if (buf[1] == '\\') {
                prog[pc++] = unescape(buf[2]);
            } else {
                prog[pc++] = buf[1];
            }
        }
        else if ((*buf >= '0' && *buf <= '9') || *buf == '.' || *buf == '-') { // number
            if (strchr(buf, '.')) { // float
                prog[pc++] = C_FLOAT;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                float *f = (float *)(prog + pc);
                *f = strtof(buf, NULL);
                pc += 4;
#else
                float f = strtof(buf, NULL);
                unsigned char *c = (unsigned char *)&f;
                for (int i = 3; i >= 0; i--) {
                    prog[pc++] = *(c + i);
                }
#endif
            } else if (!strncmp(buf, "0x", 2)) { // byte as hex
                prog[pc++] = strtol(buf, NULL, 16);
            } else { // int
                prog[pc++] = C_INT;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                short *s = (short *)(prog + pc);
                *s = (short)atoi(buf);
                pc += 2;
#else
                short s = atoi(buf);
                unsigned char *c = (unsigned char *)&s;
                prog[pc++] = *(c + 1);
                prog[pc++] = *c;
#endif
            }
        }
        else if (*buf == '\"') { // string
            prog[pc++] = C_STRING;
            for (int i = 1; i < strlen(buf) - 1; i++) {
                if (buf[i] == '\\') {
                    i++;
                    prog[pc++] = unescape(buf[i]);
                } else {
                    prog[pc++] = buf[i];
                }
            }  
            prog[pc++] = '\0'; // terminating zero
        }
        else { // command
            for (int i = 0; i < noOfInstr; i++) {
                if (!strcasecmp(buf, instrSet[i].name)) {
                    prog[pc++] = instrSet[i].number;
                    command = 1;
                    break;
                }
            }
            if (command) {
                switch (prog[pc - 1]) {
                case C_IF:
                    pc--; // back up one position
                    break; 
                case C_THEN:
                    prog[pc - 1] = C_IF;
                    *if_stack = pc++; // store current PC and skip one position, to be filled in later
                    if_stack++;
                    break;
                case C_ELSE:
                    if_stack--;
                    prog[*if_stack] = pc - *if_stack - 2; // fill in size of IF-part
                    *if_stack = pc++; // store current PC and skip one position, to be filled in later
                    if_stack++;
                    break;
                case C_ENDIF:
                case C_ENDWHILE:
                    if_stack--;
                    prog[*if_stack] = pc - *if_stack - 2; // fill in size of IF, ElSE or DO-part
                    break;
                case C_WHILE:
                    *if_stack = --pc; // back up one position, store current PC
                    if_stack++;
                    break;
                case C_DO:
                    prog[pc - 1] = C_WHILE;
                    if_stack--;
                    prog[pc] = pc - *if_stack - 1; // fill in size of WHILE-part
                    pc++;
                    *if_stack = pc++; // store current PC and skip one position, to be filled in later
                    if_stack++;
                    break;
                }
            } else { // variable name
                prog[pc++] = *buf;
            }
        }
    }
    fclose(file);
    printf("Converted size = %d bytes\n", pc);
	
    // check serial port
#ifdef _WIN32
    // For COM ports greater than 9: see https://support.microsoft.com/en-us/topic/howto-specify-serial-ports-larger-than-com9-db9078a5-b7b6-bf00-240f-f749ebfd913e
    char serialPort[BUFSIZE];
    snprintf(serialPort, BUFSIZE, "\\\\.\\%s", argv[2]);
    HANDLE h = CreateFile(serialPort, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
#else // Linux and MacOS
    int h = open(argv[2], O_RDONLY | O_NONBLOCK);
    if (h == -1) {
#endif
        printf("Cannot open port \"%s\".\n", argv[2]);
        return -1;
    }
#ifdef _WIN32
    CloseHandle(h);
#else // Linux and MacOS
    close(h);
#endif
    printf("Opening %s\n", argv[2]);

    // connect to Arduino
#ifdef _WIN32
    h = CreateFile(serialPort, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    dcbSerialParams.BaudRate = BPS;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    SetCommState(h, &dcbSerialParams);
    COMMTIMEOUTS timeoutParams;
    timeoutParams.ReadIntervalTimeout = 2; // wait 2 ms for each character (9600 bps = 1.04 ms per character)
    SetCommTimeouts(h, &timeoutParams);

    // reset Arduino
    EscapeCommFunction(h, SETDTR);
    usleep(100000);
    EscapeCommFunction(h, CLRDTR);
#else // Linux and MacOS
    h = open(argv[2], O_RDWR | O_NONBLOCK);
    struct termios settings;
    tcgetattr(h, &settings);
    cfsetispeed(&settings, BPS);
    cfsetospeed(&settings, BPS);
    settings.c_cflag &= ~CSIZE; // clear character size bits
    settings.c_cflag |= CLOCAL | CS8; // ignore modem status lines, 8 bit characters
    settings.c_oflag &= ~OPOST; // turn off various terminal features ("raw" mode)
    settings.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN); // non-canonical mode
    tcsetattr(h, TCSANOW, &settings);
    
    // reset Arduino
    int flags;
    ioctl(h, TIOCMGET, &flags);
    flags |= TIOCM_DTR;
    ioctl(h, TIOCMSET, &flags);
    usleep(100000);
    flags &= ~TIOCM_DTR;
    ioctl(h, TIOCMSET, &flags);    
#endif
    sleep(2); // wait for prompt
    readAll(h, buf); // read prompt
    printf("Response: %s\n", buf);
    printf("Erasing file \"%s\"\n", argv[1]);
    snprintf(buf, BUFSIZE, "ERASE %s", argv[1]);
    writeLine(h, buf);
    usleep(500000);
    readAll(h, buf); // read answer
    printf("Response: %s\n", buf);
    printf("Sending file \"%s\"\n", argv[1]);
    snprintf(buf, BUFSIZE, "STORE %s %d", argv[1], pc);
    writeLine(h, buf);
    writeBuffer(h, prog, pc); // write data
    usleep(500000);
    readAll(h, buf); // read answer
    printf("Response: %s\n", buf);
#ifdef _WIN32
    CloseHandle(h);
#else // Linux and MacOS
    close(h);
#endif
}
