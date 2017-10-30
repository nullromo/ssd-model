import math;
import sys;
import ast;
import re;
import random;

"""
SSD Commands:
    write <address> <data>
        writes the data to memory at the given address
        address: needs to be an integer between 0 and MEMORY_CAPACITY
        data: needs to be in the format [1,2,3] with no spaces
    read <address> <length>
        reads and returns data of a specified length at a specified address
        address: needs to be an integer between 0 and MEMORY_CAPACITY
        length: needs to be an integer and the read can't run over MEMORY_CAPACITY
    print [all | memory | state | map]
        'print all' or 'p' prints everything
        'print memory' or 'print mem' prints just the physical blocks
        'print state' prints physical_page_state
        'print map' prints the page_map
    exit
        'exit' or 'e' or 'quit' or 'q' exits the program
    garbage
        runs the garbage collection process
    expect <address> <length> <data>
        reads data of a specified length from a specified address and compares
            it to the given data. Quits the program if the expectation fails
        address: needs to be an integer between 0 and MEMORY_CAPACITY
        length: needs to be an integer and the read can't run over MEMORY_CAPACITY
        data: needs to be in the format [1,2,3] with no spaces
"""

#TODO: more efficient garbage collection
#TODO: wear-tracking (make counts of how many times things have been erased)
#TODO: wear-leveling (the find_available_page function needs to find the right pages)
#TODO: reading memory that hasn't been written to should probably return all zeros very quickly
#TODO: page_state_store and such should maybe be ranges at a time. Not sure we even care about these.
#TODO: error handling when memory is full
#TODO: make separate storage files and allow user to specify (not urgent)
#TODO: serialize memory array and maps

# parameters
NUM_BLOCKS = 4;
PAGES_PER_BLOCK = 4;# = 128;
BYTES_PER_PAGE = 8;# = 2000;

# convenience variables
BYTES_PER_BLOCK = None;
MEMORY_CAPACITY = None;

# class to represent an enum (since enums were not untroduced until Python 3)
class Enum(set):
    def __getattr__(self, name):
        if name in self:
            return name;
        else:
            raise AttributeError;

# enum for possible states of a physical memory page
PageState = Enum(['AVAILABLE', 'IN_USE', 'INVALID']);

# the actual array that represents the physical memory
blocks = None;

# keeps track of whether each page is AVAILABLE, IN_USE, or INVALID
physical_page_state = None;

# maps virtual page locations to where they are being stored
page_map = None;

# holds a trace of the actions that an operation incurred
log = list();

# performs a read from the memory of arbitrary length starting from any byte address
def read(address, length):
    read_data = list();
    check_address('memory', address, length);
    while length > 0:
        block_address = address // BYTES_PER_BLOCK;
        byte_address_in_block = address % BYTES_PER_BLOCK;
        page_address = byte_address_in_block // BYTES_PER_PAGE;
        byte_address_in_page = byte_address_in_block % BYTES_PER_PAGE;
        data = read_page(block_address, page_address);
        if data == None:
            data = [0] * BYTES_PER_PAGE;
        begin_index = byte_address_in_page;
        end_index = min(length + begin_index - 1, BYTES_PER_PAGE - 1);
        read_data.extend(data[begin_index:end_index + 1]);
        address += (BYTES_PER_PAGE - begin_index);
        length -= (BYTES_PER_PAGE - begin_index);
    return read_data;

# reads a single page from memory
def read_page(block_address, page_address):
    check_address('block', block_address);
    check_address('page', page_address);
    page_location = page_map_get(block_address, page_address);
    if page_location == None:
        return None;
    log.append('page read');
    block_address, page_address = page_location;
    return blocks[block_address][page_address][:];

# performs a write to memory using an arbitrary array of data starting from any byte address
def write(address, data, avoid=None):
    check_address('memory', address, len(data));
    while len(data) > 0:
        block_address = address // BYTES_PER_BLOCK;
        byte_address_in_block = address % BYTES_PER_BLOCK;
        page_address = byte_address_in_block // BYTES_PER_PAGE;
        byte_address_in_page = byte_address_in_block % BYTES_PER_PAGE;
        begin_index = byte_address_in_page;
        end_index = min(len(data) + begin_index - 1, BYTES_PER_PAGE - 1);
        if begin_index != 0 or end_index != BYTES_PER_PAGE:
            page = read_page(block_address, page_address);
            if page != None:
                page[begin_index:end_index + 1] = data[0:end_index - begin_index + 1];
                physical_address = page_map_get(block_address, page_address);
                page_state_store(physical_address[0], physical_address[1], PageState.INVALID);
                check_reset_block(physical_address[0]);
            else:
                page = [0] * BYTES_PER_PAGE;
                page[begin_index:end_index + 1] = data[0:end_index - begin_index + 1];
        else:
            page = data[0:BYTES_PER_PAGE + 1];
        store_location = find_available_page(avoid=avoid);
        page_map_store(block_address, page_address, store_location);
        write_page(block_address, page_address, page);
        address += (BYTES_PER_PAGE - begin_index);
        data = data[end_index - begin_index + 1:];
    return;

# writes a single page to memory
def write_page(block_address, page_address, page):
    check_address('block', block_address);
    check_address('page', page_address);
    block_address, page_address = page_map_get(block_address,page_address);
    log.append('page write');
    blocks[block_address][page_address] = page;
    page_state_store(block_address, page_address, PageState.IN_USE);
    return;

# finds an available location in physical memory for a page to be stored
def find_available_page(avoid=None, collect_garbage=True):
    log.append('page search');
    for block_num, block in enumerate(physical_page_state):
        if block_num == avoid:
            continue;
        for page_num, state in enumerate(block):
            if state == PageState.AVAILABLE:
                return (block_num, page_num);
    if collect_garbage:
        garbage_collection();
        find_available_page(collect_garbage=False);
    raise RuntimeError('no available pages');

# moves things around to create an eraseable block
def garbage_collection():
    # find block with the most invalid pages
    max_invalid_pages = 0;
    garbage_block_address = 0;
    for block_address in range(NUM_BLOCKS):
        invalid_pages = count_invalid_pages(block_address);
        max_invalid_pages = max(max_invalid_pages, invalid_pages);
        if max_invalid_pages == invalid_pages:
            garbage_block_address = block_address;
    # use block number garbage_block_address and write its valid pages to other locations
    for page_address in range(PAGES_PER_BLOCK):
        state = page_state_get(garbage_block_address, page_address);
        if state == PageState.IN_USE:
            data = blocks[garbage_block_address][page_address];
            old_address = get_virtual_page((garbage_block_address, page_address));
            write(old_address, data, avoid=garbage_block_address);
        if state != PageState.INVALID:
            page_state_store(garbage_block_address, page_address, PageState.INVALID);
    # clear the garbage block
    check_reset_block(garbage_block_address);
    return;

# returns the byte address of the virtual page that is currently mapped to a given physical page
def get_virtual_page(physical_location):
    for block_address in range(NUM_BLOCKS):
        for page_address in range(PAGES_PER_BLOCK):
            if page_map[block_address][page_address] == physical_location:
                return BYTES_PER_BLOCK*block_address + BYTES_PER_PAGE*page_address;
    raise RuntimeError('Mapping does not exist');

# returns the number of invalid pages in a physical block
def count_invalid_pages(block_address):
    invalid_pages = 0;
    for page_address, page in enumerate(blocks[block_address]):
        if page_state_get(block_address, page_address) == PageState.INVALID:
            invalid_pages += 1;
    return invalid_pages;

# resets a block if it needs to be reset (if the whole block is INVALID)
def check_reset_block(block_address):
    for i in range(PAGES_PER_BLOCK):
        if page_state_get(block_address, i) != PageState.INVALID:
            return;
    erase(block_address);
    return;

# performs an erase of a single block
def erase(block_address):
    check_address('block', block_address);
    log.append('erase');
    for i in range(PAGES_PER_BLOCK):
        blocks[block_address][i] = [0] * BYTES_PER_PAGE;
        page_state_store(block_address, i, PageState.AVAILABLE);
    return;

# checks an address to see if it is within the range of the container it is addressing
def check_address(address_type, address, length=1):
    if address_type == 'block':
        assert 0 <= address and address < NUM_BLOCKS;
    elif address_type == 'page':
        assert 0 <= address and address < PAGES_PER_BLOCK;
    elif address_type == 'memory':
        assert 0 <= address and address + length <= MEMORY_CAPACITY;
    else:
        assert False;
    return;

# gets a location out of the page map
def page_map_get(block_address, page_address):
    #log.append('check page map');
    return page_map[block_address][page_address];

# stores a location in the page map
def page_map_store(block_address, page_address, value):
    #log.append('update page map');
    page_map[block_address][page_address] = value;
    return;

# gets the state of a physical page
def page_state_get(block_address, page_address):
    #log.append('check page state');
    return physical_page_state[block_address][page_address];

# stores a state for a physical page
def page_state_store(block_address, page_address, state):
    #log.append('update page state');
    physical_page_state[block_address][page_address] = state;
    return;

# prints the state of the SSD
def print_SSD(what='all'):
    if what in ['all', 'mem','memory']:
        print_memory();
        print
    if what in ['all', 'state']:
        print_page_state();
        print
    if what in ['all', 'map']:
        print_page_map();
    return;

# prints the page map
def print_page_map():
    print 'PAGE MAP'
    for block_num, block in enumerate(page_map):
        print 'BLOCK ' + str(block_num)
        for page_num, mapping in enumerate(block):
            print '  PAGE ' + str(page_num) + ': ' + str(mapping)

# prints the page state
def print_page_state():
    print 'PHYSICAL PAGE STATE'
    for block_num, block in enumerate(physical_page_state):
        print 'BLOCK ' + str(block_num)
        for page_num, state in enumerate(block):
            print '  PAGE ' + str(page_num) + ': ' + state

# prints the physical memory
def print_memory():
    print 'PHYSICAL MEMORY'
    for block_num, block in enumerate(blocks):
        print 'BLOCK ' + str(block_num)
        for page_num, page in enumerate(block):
            sys.stdout.write('  PAGE ' + str(page_num));
            for x in page:
                sys.stdout.write('\t' + str(x));
            print

# initializes the memory with certain parameters
def init(num_blocks, pages_per_block, bytes_per_page):
    global NUM_BLOCKS, PAGES_PER_BLOCK, BYTES_PER_PAGE, BYTES_PER_BLOCK, MEMORY_CAPACITY, blocks, physical_page_state, page_map;
    NUM_BLOCKS = num_blocks;
    PAGES_PER_BLOCK = pages_per_block;
    BYTES_PER_PAGE = bytes_per_page;
    BYTES_PER_BLOCK = BYTES_PER_PAGE * PAGES_PER_BLOCK;
    MEMORY_CAPACITY = BYTES_PER_BLOCK * NUM_BLOCKS;
    blocks = [[[0] * BYTES_PER_PAGE for _ in range(PAGES_PER_BLOCK)] for __ in range(NUM_BLOCKS)];
    physical_page_state = [[PageState.AVAILABLE] * PAGES_PER_BLOCK for _ in range(NUM_BLOCKS)];
    page_map = [[None] * PAGES_PER_BLOCK for _ in range(NUM_BLOCKS)]; 

# runs the memory through a series of tests specified by a filename
def test(filename):
    with open(filename, 'r') as testfile:
        num_blocks = int(re.sub(r'.*=', '', testfile.readline()).strip());
        pages_per_block = int(re.sub(r'.*=', '', testfile.readline()).strip());
        bytes_per_page = int(re.sub(r'.*=', '', testfile.readline()).strip());
        init(num_blocks, pages_per_block, bytes_per_page);
        for command in testfile.readlines():
            execute(command);

# runs an i/o operation on the SSD
def main(operation, arg1, arg2):
    if operation == 'read':
        print read(int(arg1), int(arg2));
    elif operation == 'write':
        write(int(arg1), arg2);
    else:
        raise RuntimeError
    print log

# executes an SSD operation from a user command
def execute(command):
    del log[:]
    if command.startswith('read'):
        args = command.split(' ');
        print read(int(args[1]), int(args[2]));
        print log
    elif command.startswith('write'):
        args = command.split(' ');
        if args[2].startswith('random'):
            length = int(args[2].replace('random(','').replace(')',''));
            data = random.sample(range(256), length);
        else:
            data = ast.literal_eval(args[2]);
        write(int(args[1]), data);
        print log
    elif command.startswith('print') or command == 'p':
        args = (command + ' all').split(' ');
        print_SSD(args[1]);
    elif command in ['exit', 'e', 'q', 'quit']:
        exit();
    elif command == 'garbage':
        garbage_collection();
    elif command.startswith('expect'):
        args = command.split(' ');
        if read(int(args[1]), int(args[2])) != ast.literal_eval(args[3]):
            print 'FAILED'
            exit();
        else:
            print 'Success: ' + command
    else:
        print 'Invalid command'
    return;

usage="""
Usage: 
 To run a series of tests, use
   python ssd.py test <testfile name>

 To run a single i/o operation, use
   python ssd.py [read|write] <i/o parameters>

 To run a single i/o operation with specified parameters, use
   python ssd.py [read|write] <i/o parameters> <num_blocks> <pages_per_block> <bytes_per_page>

"""
if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'test':
        test(sys.argv[2]);
    elif len(sys.argv) == 4:
        init(NUM_BLOCKS, PAGES_PER_BLOCK, BYTES_PER_PAGE);
        main(sys.argv[1], sys.argv[2], sys.argv[3]);
    elif len(sys.argv) == 7:
        init(int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]));
        main(sys.argv[1], sys.argv[2], sys.argv[3]);
    else:
        print usage;
        raise RuntimeError
