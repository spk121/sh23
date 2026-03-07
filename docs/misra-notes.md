Low-level I/O API

For console, assume UART base address is 0x4000C000.
And that stdout and stdin are both this UART.
The stderr can be the same UART, or a different one, your choice.

bool UARTBusy(uint32_t ui32Base);
int32_t UARTCharGet(uint32_t ui32Base);
int32_t UARTCharGetNonBlocking(uint32_t ui32Base);
void UARTCharPut(uint32_t ui32Base, unsigned char ucData);
bool UARTCharPutNonBlocking(uint32_t ui32Base, unsigned char ucData);
bool UARTCharsAvail(uint32_t ui32Base);

Alternatively, the output can be to the LCD controller.

But in development, we are using ISO C standard I/O functions for console I/O:
- getchar, putchar
When using the ISO C getchar and putchar, they need to be unbuffered, to perform more like UARTCharGet and UARTCharPut.
`            setvbuf(stdin, NULL, _IONBF, 0);
            setvbuf(stdout, NULL, _IONBF, 0);`

For flash I/O, assume a simple block device. You need to erase a full block before writing to it.
Reading is trivial since it is just memory-mapped.

int32_t FlashErase(uint32_t ui32Address); // Erases a full block at the given address
int32_t FlashProgram(uint32_t ui32Address, const uint8_t *pui8Data, uint32_t ui32Length); // Length must be a multiple of 4 bytes
// No read function needed, just read from memory directly. But you can define a function if you want.
int32_t FlashRead(uint32_t ui32Address, uint8_t *pui8Data, uint32_t ui32Length); // Optional, length must be a multiple of 4 bytes

On top of the low-level Flash API, we use littlefs or the FAT-16 library to provide a filesystem abstraction.
The filesystem abstraction API is the littlefs API or the FAT-16 library API.

---

With the UART console I/O and the filesystem abstraction, implement a version of ISO C standard library functions for file I/O and console I/O.

These include:
- fopen, fclose
- fread, fwrite
- fseek, ftell
- fprintf, fscanf
- getchar, putchar
- printf, scanf
- etc.

We also need to implement file operations:
- remove, rename
- stat

We also need to implement the basic directory operations:
- opendir, readdir, closedir
- mkdir, rmdir
- rename
- chdir, getcwd
- etc.

---
Let's talk about the conflict between ISO C I/O and MISRA C I/O.

ISO C only provides buffered I/O via FILE* streams.
MISRA C Rule 21.6 requires unbuffered I/O functions.
These are conflicting requirements. It is not possible to do I/O in MISRA C without violating ISO C, and vice versa.

The goal of the project has always been to live in the ISO C world, and avoid MISRA C violations as much as possible.
Therefore, we will re-implement the ISO C standard library I/O functions as specified above, but on top of the low-level unbuffered UART and filesystem APIs, or when in development, on top of unbuffered character I/O functions like getchar and putchar and standard file I/O functions like fgetc and fputc.

N.B., there is not way to do non-blocking unbuffered input with the ISO C standard library.
The getchar function is blocking.
