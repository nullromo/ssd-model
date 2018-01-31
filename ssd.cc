#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <cstdlib>
#include <string>

// parameters copied from blockdev.h
// TODO: include properly
#define SECTOR_SIZE 32 //512 // bytes per sector
const int BYTES_PER_SECTOR = SECTOR_SIZE;
#define SECTOR_SHIFT 9
#define SECTOR_BEATS (SECTOR_SIZE / 8) // = 64 64-bit (8-byte) chunks per sector
const int INTS_PER_SECTOR = SECTOR_BEATS;
#define MAX_REQ_LEN 16 // sectors per request
FILE *_file;
const char * filename = "disk.dat";
std::string example_data = "The quick brown fox jumps over the lazy dog. If Tid the thistle sifter lifts his sieve of sifted thistles, he'll shift the thistles in his sifting sieve to live unsifted.";

// ssd parameters and convenience variables
const int NUM_BLOCKS = 4; // total number of blocks in the SSD
const int PAGES_PER_BLOCK = 4; // number of pages in each block
const int INTS_PER_PAGE = 8; // number of 64-bit integers in each page
const int BYTES_PER_INT = 8; // number of bytes in a 64-bit integer
const int BYTES_PER_PAGE = INTS_PER_PAGE * BYTES_PER_INT; // total bytes in 1 page
const int BYTES_PER_BLOCK = BYTES_PER_PAGE * PAGES_PER_BLOCK; // total bytes in 1 block
const int MEMORY_CAPACITY_INTS = INTS_PER_PAGE * PAGES_PER_BLOCK; // total storage in ints
const int MEMORY_CAPACITY_BYTES = BYTES_PER_BLOCK * NUM_BLOCKS; // total storage in bytes
const int SECTORS_PER_PAGE = INTS_PER_PAGE / INTS_PER_SECTOR; // number of sectors in each page

/*
 * An enum to represent the state of a page on the disk.
 */
enum class PageState { AVAILABLE=0, IN_USE=1, INVALID=2 };
const char * name[] = {"AVAILABLE", "IN_USE", "INVALID"};

/*
 * Holds an address for a page in the SSD. This is a block address and a page
 * address within that block. It can be a physical address or a virtual address.
 */
struct ssd_address
{
    uint32_t block_address;
    uint32_t page_address;

    ssd_address()
    {
        block_address = -1;
        page_address = -1;
    }
};

/*
 * Maps virtual pages to physical pages. For example, specifying page_map[2][3]
 * will return the physical page that is backing virtual page 3 of virtual
 * block 2.
 */
ssd_address page_map[NUM_BLOCKS][PAGES_PER_BLOCK];

/*
 * Stores the state of each virtual page. If a page is invalid, it means that
 * the data in it is old. The page map should not direct any virtual pages to an
 * invalid physical page. If a page is in use, that means the page map is
 * currently mapping a virtual page to it. If a page is available, that means it
 * has been cleared and is waiting for the page map to start using it.
 */
PageState physical_page_state[NUM_BLOCKS][PAGES_PER_BLOCK];

/*
 * Prints a string with a newline.
 */
template<class T>
void p(T string)
{
    std::cout << string << "\n";
}

/*
 * Defines the min function
 */
int min(int a, int b)
{
    if(a <= b)
        return a;
    return b;
}

/*
 * Prints the first N elements of an array.
 */
void print_array(uint64_t data[], int n, bool characters=false)
{
    printf("[");
    for(int i=0; i<n-1; i++)
    {
        printf(characters?"%c, ":"%d, ", (int) data[i]); 
    }
    printf(characters?"%c]\n":"%d]\n", (int) data[n]);
}

/*
 * Takes a physical ssd_address and returns its offset in bytes.
 */
uint32_t byte_offset_of(ssd_address physical_address)
{
    return physical_address.block_address * BYTES_PER_BLOCK + physical_address.page_address * BYTES_PER_PAGE;
}

/*
 * Takes a byte offset and returns its corresponding ssd address
 */
ssd_address address_of(uint64_t offset)
{
    ssd_address address;
    address.block_address = offset / BYTES_PER_BLOCK;
    address.page_address = (offset % BYTES_PER_BLOCK) / BYTES_PER_PAGE;
    return address;
}

/*
 * Reads data from the file and fills it into a buffer.
 * offset: number of bytes from the beginning of the file.
 * buffer: array that the resulting data gets put into.
 * length: number of sectors to read.
 */
void from_disk(uint64_t offset, uint64_t buffer[], uint32_t length)
{
    int error_code = fseek(_file, offset, SEEK_SET);
    if(error_code)
        p("Could not seek.");
    int sectors_read = fread(buffer, BYTES_PER_SECTOR, length, _file);
    if(sectors_read < length)
        p("Didn't read enough sectors.");
}

/*
 * Writes data from a buffer to the file.
 * offset: number of bytes from the beginning of the file.
 * data: array that the data comes from.
 * count: number of 64-bit integers being written.
 */
void to_disk(uint64_t offset, uint64_t data[], uint64_t count)
{
    int error_code = fseek(_file, offset, SEEK_SET);
    if(error_code)
        p("Could not seek.");
    int sectors_written = fwrite(data, sizeof(uint64_t), count, _file);
    if(sectors_written < count)
        p("Didn't write enough sectors.");
}

/*
 * Gets the state of a physical page out of the state status array.
 */
PageState page_state_get(ssd_address physical_address)
{
    return physical_page_state[physical_address.block_address][physical_address.page_address];
}

/*
 * Stores the state of a physical page in the physical page state status array.
 */
void page_state_store(ssd_address physical_address, PageState state)
{
    physical_page_state[physical_address.block_address][physical_address.page_address] = state;
}

/*
 * Gets the physical page backing a virtual page.
 */
ssd_address page_map_get(ssd_address virtual_address)
{
    return page_map[virtual_address.block_address][virtual_address.page_address];
}

/*
 * Stores the physical location of a virtual page in the page map.
 */
void page_map_store(ssd_address virtual_address, ssd_address physical_address)
{
    page_map[virtual_address.block_address][virtual_address.page_address] = physical_address;
}

/*
 * Looks through the SSD to find a page that is marked as AVAILABLE.
 */
ssd_address find_available_page(uint32_t avoid)
{
    ssd_address store_location;
    store_location.block_address = 0;
    store_location.page_address = 0;
    for(int block_num=0; block_num<NUM_BLOCKS; block_num++)
    {
        if(block_num == avoid)
            continue;
        for(int page_num=0; page_num<PAGES_PER_BLOCK; page_num++)
        {
            store_location.block_address = block_num;
            store_location.page_address = page_num;
            if(page_state_get(store_location) == PageState::AVAILABLE)
                return store_location;
        }
    }
}

/*
 * Writes a single page to memory. The length of the data array should be the
 * length of 1 page.
 */
void write_page(ssd_address virtual_address, uint64_t data[], uint32_t avoid=-1)
{
    ssd_address physical_address = find_available_page(avoid);
    page_map_store(virtual_address, physical_address);
    to_disk(byte_offset_of(physical_address), data, INTS_PER_PAGE);
    p("page write");
    page_state_store(physical_address, PageState::IN_USE);
}

/*
 * Reads a single page from memory.
 */
void read_page(uint64_t buffer[], ssd_address virtual_address)
{
    ssd_address physical_address = page_map_get(virtual_address);
    from_disk(byte_offset_of(physical_address), buffer, SECTORS_PER_PAGE);
    p("page read");
}

/*
 * Erases a block.
 */
void erase_block(uint64_t block_address)
{
    p("erase");
    ssd_address address;
    address.block_address = block_address;
    for(int i=0; i<PAGES_PER_BLOCK; i++)
    {
        address.page_address = i;
        page_state_store(address, PageState::AVAILABLE);
    }
}

/*
 * Checks if the given block needs to be reset and resets it if necessary.
 */
void check_reset_block(uint64_t block_address)
{
    ssd_address address;
    address.block_address = block_address;
    for(int i=0; i<PAGES_PER_BLOCK; i++)
    {
        address.page_address = i;
        if(page_state_get(address) != PageState::INVALID)
            return;
    }
    erase_block(block_address);
}

/*
 * Reads from the memory the way the block device should, ignoring the way the
 * SSD works. Interfaces with the block device model.
 * offset: number of bytes into the memory
 * buffer: location for the data to end up. Must be at least COUNT ints long, rounded up to the nearest whole page
 * count: number of ints to be read
 */
void read(uint64_t offset, uint64_t buffer[], int count)
{
    while(count > 0)
    {
        ssd_address virtual_address = address_of(offset);
        read_page(buffer, virtual_address);
        count -= INTS_PER_PAGE;
        buffer += INTS_PER_PAGE;
        offset += BYTES_PER_PAGE;
    }
}

/*
 * Writes to the memory the way the block device should, ignoring the way the
 * SSD works. Interfaces with the block device model.
 * offset: number of bytes into the memory
 * data: array of ints to be written
 * count: number of ints to be written
 */
void write(uint64_t offset, uint64_t data[], int count)
{
    //while there's still data to write
    while(count > 0)
    {
        ssd_address virtual_address = address_of(offset);
        ssd_address physical_address = page_map_get(virtual_address);
        uint64_t page_offset = offset % BYTES_PER_PAGE;
        uint64_t page[INTS_PER_PAGE];
        p("count");
        p(count);
        p("addresses (v, p)");
        p(virtual_address.block_address);
        p(virtual_address.page_address);
        p(physical_address.block_address);
        p(physical_address.page_address);
        //if there was data there already, read it and merge it with the new data
        if(physical_address.block_address != -1 && page_state_get(physical_address) == PageState::IN_USE)
        {
            p("page was in use");
            read_page(page, virtual_address);
            //mark the old physical location as invalid
            page_state_store(physical_address, PageState::INVALID);
            check_reset_block(physical_address.block_address);
        }
        for(int i=page_offset; i<min(page_offset + count, INTS_PER_PAGE); i++)
        {
            page[i] = data[i];
        }
        write_page(virtual_address, page);
        count -= INTS_PER_PAGE;
        data += INTS_PER_PAGE;
        offset += BYTES_PER_PAGE;
    }
}

/*
 * Prints everything.
 */
void print_ssd()
{
    p("PHYSICAL MEMORY");
    //print the contents of the memory
    for (int block_num=0; block_num<NUM_BLOCKS; block_num++)
    {
        printf("BLOCK %d   SECTORS\n", block_num);
        for (int page_num=0; page_num<PAGES_PER_BLOCK; page_num++)
        {
            printf("  PAGE %d  ", page_num);
            for (int sector_num=0; sector_num<SECTORS_PER_PAGE; sector_num++)
            {
                uint64_t sector[INTS_PER_SECTOR];
                from_disk(block_num*BYTES_PER_BLOCK+page_num*BYTES_PER_PAGE+sector_num*BYTES_PER_SECTOR, sector, 1);
                for (int i=0; i<INTS_PER_SECTOR; i++)
                    printf("%c", (char)sector[i]);
                printf("\t");
            }
            p("");
        }
    }
    p("");
    p("PAGE MAP");
    //print the contents of the page map
    for (int block_num=0; block_num<NUM_BLOCKS; block_num++)
    {
        printf("BLOCK %d\n", block_num);
        for (int page_num=0; page_num<PAGES_PER_BLOCK; page_num++)
        {
            ssd_address loc = page_map[block_num][page_num];
            printf("  PAGE %d: (%d, %d)\n", page_num, loc.block_address, loc.page_address);
        }
    }
    p("");
    p("PAGE STATE");
    //print the contents of the page state status array
    for (int block_num=0; block_num<NUM_BLOCKS; block_num++)
    {
        printf("BLOCK %d\n", block_num);
        for (int page_num=0; page_num<PAGES_PER_BLOCK; page_num++)
        {
            PageState state = physical_page_state[block_num][page_num];
            printf("  PAGE %d: %s\n", page_num, name[static_cast<int>(state)]);
        }
    }
}

//writes a bunch of %-signs to the memory to "clear" it
void clear_mem()
{
    int write_length = NUM_BLOCKS*PAGES_PER_BLOCK*INTS_PER_PAGE;
    uint64_t data[sizeof(uint64_t) * write_length];
    for(int i=0; i<write_length; i++)
        data[i] = 37;
    to_disk(0, data, write_length);
}

//basic test of to_disk and from_disk.
void testing_1()
{
    int write_length = example_data.length();
    printf("Writing example data, length = %d\n", write_length);
    uint64_t write_data[sizeof(uint64_t) * write_length];
    for (int i=0; i<write_length; i++)
        write_data[i] = example_data[i];
    to_disk(0, write_data, write_length);

    uint64_t read_data[MAX_REQ_LEN * INTS_PER_SECTOR];
    int sectors_to_read = 2;
    printf("Reading %d sector(s): ", sectors_to_read);
    from_disk(0, read_data, sectors_to_read);
    for (int i=0; i<sectors_to_read*INTS_PER_SECTOR; i++)
        printf("%c", (int)read_data[i]);
    p("");
}

//basic test of read_page and write_page
void testing_2()
{
    ssd_address address;
    address.block_address = 0;
    address.page_address = 1;
    printf("Created address (%d,%d)\n", address.block_address, address.page_address);
    uint64_t write_data[INTS_PER_PAGE];
    for (int i=0; i<INTS_PER_PAGE; i++)
        write_data[i] = example_data[i];
    printf("Filled write buffer: ");
    for(int i=0; i<INTS_PER_PAGE; i++)
        printf("%c", (int)write_data[i]);
    p("");
    write_page(address, write_data);

    address.page_address = 1;
    int read_length = INTS_PER_PAGE;
    uint64_t read_data[sizeof(uint64_t) * read_length];
    read_page(read_data, address);
    printf("Read data from (%d,%d): ", address.block_address, address.page_address);
    for(int i=0; i<read_length; i++)
        printf("%c", (int)read_data[i]);
    p("");
}

//basic test of read() and write()
void testing_3()
{
    //write new data to the same location
    uint64_t write_data[5];
    int length = 5;
    write_data[0] = 67;
    write_data[1] = 67;
    write_data[2] = 67;
    write_data[3] = 67;
    write_data[4] = 67;
    write(0, write_data, length);
    write_data[0] = 68;
    write_data[1] = 68;
    write_data[2] = 68;
    write_data[3] = 68;
    write_data[4] = 68;
    write(0, write_data, length);
    write(BYTES_PER_PAGE * 2, write_data, length);

    //write many pages at once
    uint64_t write_data_long[40];
    length = 40;
    for(int i=0; i<length; i++)
    {
        write_data_long[i] = i + 55;
    }
    write(BYTES_PER_PAGE * 5, write_data_long, length);

    //read
    uint64_t read_data[8];
    read(0, read_data, 8);
    print_array(read_data, 8, true);

    //read many pages at once
    uint64_t read_data_long[56];
    read(0, read_data_long, 56);
    print_array(read_data_long, 56, true);
    //TODO: what happens if you want to read invalid/unwritten data? I guess it all needs to be zeroed out.
}

int main(int argc, char* argv[])
{
    _file = fopen(filename, "r+");

    //testing_1();
    //clear_mem();
    //testing_2();
    testing_3();
    print_ssd();

    return 0;
}
