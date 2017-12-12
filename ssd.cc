#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <cstdlib>
#include <string>

// parameters copied from blockdev.h
// TODO: include properly
#define SECTOR_SIZE 512 // bytes per sector
#define SECTOR_SHIFT 9
#define SECTOR_BEATS (SECTOR_SIZE / 8) // = 64 64-bit (8-byte) chunks per sector
#define MAX_REQ_LEN 16 // sectors per request
FILE *_file;
const char * filename = "disk.dat";
std::string example_data = "The quick brown fox jumps over the lazy dog. If Tid the thistle sifter lifts his sieve of sifted thistles, then all the thistles in his sifting sieve will live unsifted.";

// ssd parameters
const int NUM_BLOCKS = 4;
const int PAGES_PER_BLOCK = 4;
const int BYTES_PER_PAGE = 8;

enum PageState { AVAILABLE, IN_USE, INVALID };

/*
 * Prints a string with a newline.
 */
template<class T>
void p(T string)
{
    std::cout << string << "\n";
}

/*
 * Reads data from the file and fills it into a buffer.
 * offset: number of bytes from the beginning of the file.
 * buffer: array that the resulting data gets put into.
 * length: number of sectors to read.
 */
void read(uint64_t offset, uint64_t buffer[], uint32_t length)
{
    int error_code = fseek(_file, offset, SEEK_SET);
    if(error_code)
        p("Could not seek.");
    int sectors_read = fread(buffer, SECTOR_SIZE, length, _file);
    if(sectors_read < length)
        p("Didn't read enough sectors.");
}

/*
 * Writes data from a buffer to the file.
 * offset: number of bytes from the beginning of the file.
 * data: array that the data comes from.
 * count: number of 64-bit integers being written.
 */
void write(uint64_t offset, uint64_t data[], uint64_t count)
{
    int error_code = fseek(_file, offset, SEEK_SET);
    if(error_code)
        p("Could not seek.");
    int sectors_written = fwrite(data, sizeof(uint64_t), count, _file);
    if(sectors_written < count)
        p("Didn't write enough sectors.");
}

int main(int argc, char* argv[])
{
    _file = fopen(filename, "r+");

    uint64_t read_data[MAX_REQ_LEN * SECTOR_BEATS];
    int sectors_to_read = 1;
    read(8, read_data, sectors_to_read);
    for (int i=0; i<sectors_to_read*SECTOR_BEATS; i++)
        printf("%c", (int)read_data[i]);
    p("");

    int write_length = example_data.length();
    uint64_t write_data[sizeof(uint64_t) * write_length];
    for (int i=0; i<write_length; i++)
        write_data[i] = example_data[i];
    write(0, write_data, write_length);

    return 0;
}
