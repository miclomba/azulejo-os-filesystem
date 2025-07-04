/*******************************************************************************
 * File Sector Manager (FSM)
 * Author: Michael Lombardi
 *******************************************************************************/
#include "fsm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "fsm_constants.h"
#include "global_constants.h"
#include "inode.h"
#include "ssm.h"

// @todo look for inline opportunities
// @todo revisit return status and exception handling
// @todo replace literals with constants

static FSM fsm_instance = {.diskHandle = NULL};

FSM *fsm = &fsm_instance;

//================================ TYPES ==================================//
typedef enum PointerType { SINGLE, DOUBLE, TRIPLE } PointerType;

//========================= FSM FUNCTION PROTOTYPES =======================//
static void init_file_sector_mgr(int _initSsmMaps);
static void init_fsm_maps(void);
static Bool is_not_null(unsigned int _ptr);
static Bool is_null(unsigned int _ptr);
static Bool create_file(unsigned int _inodeNumF, unsigned int *_name, unsigned int _inodeNumD);
static Bool create_file_in_avail_indirect_loc(unsigned int _inodeNumF, unsigned int *_file_name,
                                              Bool _allocate);
static Bool create_file_in_unavail_indirect_loc(PointerType _type, unsigned int _inodeNumF,
                                                unsigned int *_file_name, unsigned int _inodeNumD,
                                                unsigned int *diskOffset,
                                                unsigned int *file_buffer);
static Bool create_file_in_avail_direct_loc(unsigned int _inodeNumF, unsigned int *_file_name,
                                            unsigned int *diskOffset, unsigned int *disk_buffer);
static Bool create_file_in_unavail_direct_loc(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                              unsigned int *_file_name, unsigned int *diskOffset,
                                              unsigned int *disk_buffer);
static Bool add_file_to_single_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _sIndirectOffset, Bool _allocate);
static Bool add_file_to_double_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _dIndirectOffset, Bool _allocate);
static Bool add_file_to_triple_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _tIndirectOffset, Bool _allocate);
static Bool add_file_to_single_indirect_no_alloc(unsigned int *buffer, unsigned int *indirectBlock,
                                                 unsigned int _inodeNumF, unsigned int *_name,
                                                 unsigned int *diskOffset);
static Bool remove_file_from_single_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _dIndirectOffset,
                                             unsigned int _sIndirectOffset);
static Bool remove_file_from_double_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _tIndirectOffset,
                                             unsigned int _dIndirectOffset);
static Bool remove_file_from_triple_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _tIndirectOffset);
static unsigned int aloc_single_indirect(long long int _blockCount);
static unsigned int aloc_double_indirect(long long int _blockCount);
static unsigned int aloc_triple_indirect(long long int _blockCount);
static void *write_to_file_direct(void *buffer, unsigned int directPtrs);
static void *write_to_file_using_single_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs);
static void *write_to_file_using_double_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs,
                                                        unsigned int *dIndirectPtrs);
static void *write_to_file_using_triple_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs,
                                                        unsigned int *dIndirectPtrs,
                                                        unsigned int *tIndirectPtrs);
static void write_to_single_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _sIndirectPtrs);
static void write_to_double_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _dIndirectPtrs);
static void write_to_triple_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _tIndirectPtrs);
static void read_from_single_indirect_blocks(void *_buffer, unsigned int _diskOffset);
static void read_from_double_indirect_blocks(void *_buffer, unsigned int _diskOffset);
static void read_from_triple_indirect_blocks(void *_buffer, unsigned int _diskOffset);
static void remove_file_single_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset);
static void remove_file_indirect_blocks(PointerType _type, unsigned int indirect,
                                        unsigned int fileType, unsigned int _inodeNum,
                                        unsigned int _inodeNumD);
static void remove_file_double_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset);
static void remove_file_triple_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset);
static Bool rename_file_in_single_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _sIndirectOffset);
static Bool rename_file_in_double_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _dIndirectOffset);
static Bool rename_file_in_triple_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _tIndirectOffset);
static Bool remove_file_from_dir_indirect_pointers(unsigned int _inodeNumF,
                                                   unsigned int _inodeNumD);

//========================= FSM FUNCTION DEFINITIONS =======================//
/**
 * @brief Verify if a pointer is not null.
 * @param[in] _ptr a pointer.
 * @return True if the pointer is not null; false otherwise.
 * @date 2025-06-17 First implementation.
 */
static inline Bool is_not_null(unsigned int _ptr) { return _ptr != (unsigned int)(-1); }

/**
 * @brief Verify if a pointer is null.
 * @param[in] _ptr a pointer.
 * @return True if the pointer is null; false otherwise.
 * @date 2025-06-17 First implementation.
 */
static inline Bool is_null(unsigned int _ptr) { return _ptr == (unsigned int)(-1); }

/**
 * @brief Initializes the File Sector Manager.
 * Sets up the File Sector Manager and optionally initializes the SSM maps.
 * @param[in] _initSsmMaps Flag indicating whether to initialize the SSM maps.
 * @return void
 * @date 2010-04-01 First implementation.
 */
static void init_file_sector_mgr(int _initSsmMaps) {
    // Initialize SEctor Space Manager
    ssm_init(_initSsmMaps);
    // Initialize FSM's variables
    inode_map.iMapOffset[0] = -1;
    inode_map.iMapOffset[1] = -1;
    // Initialize FSM's inode pointer to a blank inode
    if (inode_init(&inode) == FAILURE) {
        // @todo do something here
    }

    inode_map.id = (unsigned int)-1;
    // Open file stream for iMap
    inode_map.iMapHandle = fopen(FSM_INODE_MAP, "r+");
    // Read in INODE_BLOCKS number of items from iMap to iMapHandle
    fread(inode_map.iMap, 1, INODE_BLOCKS, inode_map.iMapHandle);
    // Closes the iMapHandle
    fclose(inode_map.iMapHandle);
    inode_map.iMapHandle = Null;
    // Open binary form of file for reading and writing
    fsm->diskHandle = fopen(HARD_DISK, "rb+");
}

/**
 * @brief Initializes the File Sector Manager maps.
 * Sets up internal maps within the File Sector Manager.
 * @return void
 * @date 2010-04-01 First implementation.
 */
static void init_fsm_maps(void) {
    unsigned char map[INODE_BLOCKS];  // SECTOR_BYTES
    unsigned char disk[DISK_SIZE];
    // Load iMap from file and place in iMapHandle
    inode_map.iMapHandle = fopen(FSM_INODE_MAP, "r+");
    // Initialize all map elements to 255
    memset(map, UINT8_MAX, INODE_BLOCKS);
    // Write map back to iMapHandle
    fwrite(map, 1, INODE_BLOCKS, inode_map.iMapHandle);
    // close the file
    fclose(inode_map.iMapHandle);
    inode_map.iMapHandle = Null;
    fsm->diskHandle = fopen(HARD_DISK, "rb+");
    // Initialize all disk elements to 0
    memset(disk, 0, DISK_SIZE);
    // Write contents of disk to diskHandle
    fwrite(disk, 1, DISK_SIZE, fsm->diskHandle);
    fclose(fsm->diskHandle);
    fsm->diskHandle = Null;
}

unsigned int fs_create_file(int _isDirectory, unsigned int *_name,
                            unsigned int _inodeNumParentDir) {
    get_inode(1);
    if (is_null(inode_map.iMapOffset[0])) {
        return (unsigned int)(-1);
    }
    // Create a default file. Will start with all pointers as -1
    unsigned int inodeNum;
    inodeNum = BITS_PER_BYTE * inode_map.iMapOffset[0] + inode_map.iMapOffset[1];
    inode_read(&inode, inodeNum, fsm->diskHandle);
    // initialize inode metadata
    if (inode_init(&inode) == FAILURE) {
        // @todo do something here
    }
    // Assign filetype
    inode.fileType = _isDirectory == 1 ? 2 : 1;

    inode_write(&inode, inodeNum, fsm->diskHandle);
    allocate_inode();
    unsigned int name[2];
    if (_isDirectory == 1) {
        // set . directory
        strcpy((char *)name, ".");
        create_file(inodeNum, name, inodeNum);
        // set .. directory
        strcpy((char *)name, "..");
        create_file(_inodeNumParentDir, name, inodeNum);
    }  // end if (_isDirectory == 1)
    create_file(inodeNum, _name, _inodeNumParentDir);
    return inodeNum;
}

Bool fs_open_file(unsigned int _inodeNum, Inode *_inode) {
    if (is_null(_inodeNum)) {
        return False;
    }
    inode_read(_inode, _inodeNum, fsm->diskHandle);
    inode_map.id = _inodeNum;
    if (_inode->fileType <= 0) {
        // If file not loaded, create a default inode and return False
        if (inode_init(_inode) == FAILURE) {
            // @todo do something here
        }
        inode_map.id = (unsigned int)(-1);
        // Return False if file did not open correctly
        return False;
    }
    return True;
}

Bool fs_close_file(void) {
    // Reset all FSM->Inode variables to defaults
    inode_map.id = (unsigned int)(-1);
    return inode_init(&inode) == FAILURE ? False : True;
}

Bool fs_read_from_file(unsigned int _inodeNum, void *_buffer) {
    // Open file at Inode _inodeNum for reading
    if (!fs_open_file(_inodeNum, &inode)) {
        return False;
    }  // end if (success == False)
    void *buffer = _buffer;
    // Read data from direct pointers into buffer _buffer
    unsigned int diskOffset;
    for (int i = 0; i < INODE_DIRECT_PTRS; i++) {
        diskOffset = inode.directPtr[i];
        if (is_not_null(diskOffset)) {
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fread(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            buffer = (char *)buffer + BLOCK_SIZE;
        }
    }  // end for (i = 0; i < INODE_DIRECT_PTRS; i++)
    // Read data from single indirect pointer into buffer _buffer
    diskOffset = inode.sIndirect;
    if (is_not_null(diskOffset)) {
        read_from_single_indirect_blocks(buffer, diskOffset);
        buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
    }
    diskOffset = inode.dIndirect;
    if (is_not_null(diskOffset)) {
        read_from_double_indirect_blocks(buffer, diskOffset);
        buffer = (char *)buffer + BLOCK_SIZE * D_INDIRECT_BLOCKS;
    }
    diskOffset = inode.tIndirect;
    if (is_not_null(diskOffset)) {
        read_from_triple_indirect_blocks(buffer, diskOffset);
    }
    fseek(fsm->diskHandle, 0, SEEK_SET);
    return True;
}

/**
 * @brief Writes data to a file that uses direct blocks.
 * Writes the contents of the provided buffer to the file identified by the given inode number.
 * @param[in,out] _buffer Pointer to the data to be written.
 * @param[in,out] directPtrs the number of direct blocks
 * @return the updated buffer address.
 * @date 2025-06-16 First implementation.
 */
static void *write_to_file_direct(void *buffer, unsigned int directPtrs) {
    unsigned int diskOffset;
    for (unsigned int i = 0; i < directPtrs; i++) {
        diskOffset = inode.directPtr[i];
        if (is_null(diskOffset)) {
            // If direct pointer is empty, get a Sector for it
            diskOffset = ssm_allocate_sectors(1);
            if (is_null(diskOffset)) {
                break;
            } else {
                inode.directPtr[i] = diskOffset;
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fwrite(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
                buffer = (char *)buffer + BLOCK_SIZE;
            }
        } else {
            // Search for diskHandle from beginning of disk
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            // Overwrite the data at that location
            fwrite(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            buffer = (char *)buffer + BLOCK_SIZE;
        }
    }  // end for (i = 0; i < directPtrs; i++)
    return buffer;
}

/**
 * @brief Writes data to a file that uses single indirect blocks.
 * Writes the contents of the provided buffer to the file identified by the given inode number.
 * @param[in,out] _buffer Pointer to the data to be written.
 * @param[in,out] _fileSize Size of the data to write, in bytes.
 * @param[in,out] baseOffset the base offset to write to
 * @param[in,out] sIndirectPtrs the number of single indirect blocks
 * @return the updated buffer address.
 * @date 2025-06-16 First implementation.
 */
static void *write_to_file_using_single_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs) {
    // Calculate number of single indirect pointers needed
    *sIndirectPtrs = *fileSize / BLOCK_SIZE;
    if (*fileSize % BLOCK_SIZE > 0) {
        *sIndirectPtrs += 1;
    }  // end if (fileSize % BLOCK_SIZE > 0)
    // Allocate single indirect pointers
    inode.sIndirect = aloc_single_indirect(*sIndirectPtrs);
    // Write to single indirect pointers
    *baseOffset = inode.sIndirect;
    write_to_single_indirect_blocks(*baseOffset, buffer, *sIndirectPtrs);
    return buffer;
}

/**
 * @brief Writes data to a file that uses double indirect blocks.
 * Writes the contents of the provided buffer to the file identified by the given inode number.
 * @param[in,out] _buffer Pointer to the data to be written.
 * @param[in,out] _fileSize Size of the data to write, in bytes.
 * @param[in,out] baseOffset the base offset to write to
 * @param[in,out] sIndirectPtrs the number of single indirect blocks
 * @param[in,out] dIndirectPtrs the number of double indirect blocks
 * @return the updated buffer address.
 * @date 2025-06-16 First implementation.
 */
static void *write_to_file_using_double_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs,
                                                        unsigned int *dIndirectPtrs) {
    // Allocate single Indirect blocks
    *sIndirectPtrs = S_INDIRECT_BLOCKS;
    inode.sIndirect = aloc_single_indirect(*sIndirectPtrs);
    // Write to single Indirect pointers
    *baseOffset = inode.sIndirect;
    write_to_single_indirect_blocks(*baseOffset, buffer, *sIndirectPtrs);
    buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
    // Calculate Remaining filesize
    *fileSize = *fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - S_INDIRECT_SIZE;
    // Calculate number of double Indirect Pointrs needed
    *dIndirectPtrs = *fileSize / BLOCK_SIZE;
    if (*fileSize % BLOCK_SIZE > 0) {
        *dIndirectPtrs += 1;
    }  // end if (fileSize % BLOCK_SIZE > 0)
    // Allocate double Indirect Pointers
    inode.dIndirect = aloc_double_indirect(*dIndirectPtrs);
    *baseOffset = inode.dIndirect;
    // Write to double Indirect Pointers
    write_to_double_indirect_blocks(*baseOffset, buffer, *dIndirectPtrs);

    return buffer;
}

/**
 * @brief Writes data to a file that uses triple indirect blocks.
 * Writes the contents of the provided buffer to the file identified by the given inode number.
 * @param[in,out] _buffer Pointer to the data to be written.
 * @param[in,out] _fileSize Size of the data to write, in bytes.
 * @param[in,out] baseOffset the base offset to write to
 * @param[in,out] sIndirectPtrs the number of single indirect blocks
 * @param[in,out] dIndirectPtrs the number of double indirect blocks
 * @param[in,out] tIndirectPtrs the number of triple indirect blocks
 * @return the updated buffer address.
 * @date 2025-06-16 First implementation.
 */
static void *write_to_file_using_triple_indirect_blocks(void *buffer, long long int *fileSize,
                                                        unsigned int *baseOffset,
                                                        unsigned int *sIndirectPtrs,
                                                        unsigned int *dIndirectPtrs,
                                                        unsigned int *tIndirectPtrs) {
    *sIndirectPtrs = S_INDIRECT_BLOCKS;
    *dIndirectPtrs = D_INDIRECT_BLOCKS;
    // Allocate Single and double indirect pointers
    inode.sIndirect = aloc_single_indirect(*sIndirectPtrs);
    inode.dIndirect = aloc_double_indirect(*dIndirectPtrs);
    // Write to single indirect blocks
    *baseOffset = inode.sIndirect;
    write_to_single_indirect_blocks(*baseOffset, buffer, *sIndirectPtrs);
    buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
    // Write to double indirect blocks
    *baseOffset = inode.dIndirect;
    write_to_double_indirect_blocks(*baseOffset, buffer, *dIndirectPtrs);
    buffer = (char *)buffer + BLOCK_SIZE * D_INDIRECT_BLOCKS;
    // Calculate remaining filesize
    *fileSize = *fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - D_INDIRECT_SIZE;
    // Calculate number of triple Indirect pointers needed
    *tIndirectPtrs = *fileSize / BLOCK_SIZE;
    if (*fileSize % BLOCK_SIZE > 0) {
        *tIndirectPtrs += 1;
    }  // end if (fileSize % BLOCK_SIZE > 0)
    // Allocate triple Indirect Pointers
    inode.tIndirect = aloc_triple_indirect(*tIndirectPtrs);
    *baseOffset = inode.tIndirect;
    // Write to tripleIndirectBlocks
    write_to_triple_indirect_blocks(*baseOffset, buffer, *tIndirectPtrs);

    return buffer;
}

Bool fs_write_to_file(unsigned int _inodeNum, void *_buffer, long long int _fileSize) {
    // return false if openFile fails
    if (!fs_open_file(_inodeNum, &inode)) {
        return False;
    }  // end if (success == False)
    // set fileSize and inode fileSize
    long long int fileSize = _fileSize;
    inode.fileSize = (unsigned int)_fileSize;
    inode.dataBlocks = inode.fileSize / BLOCK_SIZE;
    unsigned int directPtrs = 0;
    // Calculate how many direct pointers needed
    for (unsigned int i = 0; i < INODE_DIRECT_PTRS; i++) {
        fileSize -= BLOCK_SIZE;
        directPtrs++;
        if (fileSize <= 0) {
            break;
        }  // end else (fileSize > 0)
    }  // end for (i = 0; i < INODE_DIRECT_PTRS; i++)
    void *buffer = _buffer;
    // Write to direct pointers
    buffer = write_to_file_direct(buffer, directPtrs);

    unsigned int sIndirectPtrs = 0;
    unsigned int dIndirectPtrs = 0;
    unsigned int tIndirectPtrs = 0;
    unsigned int baseOffset;
    // If file is too big for the direct pointers,
    // begin looking at the indrect pointers
    if (fileSize > 0) {
        // Write to file using triple Indirection if file too big for
        /// double indirection
        if (fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - D_INDIRECT_SIZE > 0) {
            buffer = write_to_file_using_triple_indirect_blocks(
                buffer, &fileSize, &baseOffset, &sIndirectPtrs, &dIndirectPtrs, &tIndirectPtrs);

        }  // end if (fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - D_INDIRECT_SIZE > 0)
        // Write to file using double Indirection if too big for
        //   single indirection
        else if (fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - S_INDIRECT_SIZE > 0) {
            buffer = write_to_file_using_double_indirect_blocks(buffer, &fileSize, &baseOffset,
                                                                &sIndirectPtrs, &dIndirectPtrs);
        }  // end else if (fileSize + (INODE_DIRECT_PTRS * BLOCK_SIZE) - S_INDIRECT_SIZE > 0)
        // Write to file using single Indirection only
        else {
            buffer = write_to_file_using_single_indirect_blocks(buffer, &fileSize, &baseOffset,
                                                                &sIndirectPtrs);
        }  // end else
    }  // end if (fileSize > 0)
    // Write created inode to disk
    inode_write(&inode, _inodeNum, fsm->diskHandle);
    fseek(fsm->diskHandle, 0, SEEK_SET);
    return True;
}

/**
 * @brief Writes a file to an inode's first available indirect pointer location.
 * Writes a file into the specified inode memory within the File Sector Manager.
 * @param[in] _inodeNumF Inode number of the file to be written.
 * @param[in] _file_name Pointer to the name of the file.
 * @param[in] _allocate If true, allocate SSM blocks as needed.
 * @return True if the file was written successfully, false otherwise.
 * @date 2025-06-17 First implementation.
 */
static Bool create_file_in_avail_indirect_loc(unsigned int _inodeNumF, unsigned int *_file_name,
                                              Bool _allocate) {
    if (is_not_null(inode.sIndirect)) {
        if (add_file_to_single_indirect(_inodeNumF, _file_name, inode.sIndirect, _allocate))
            return True;
    }
    if (is_not_null(inode.dIndirect)) {
        if (add_file_to_double_indirect(_inodeNumF, _file_name, inode.dIndirect, _allocate))
            return True;
    }
    if (is_not_null(inode.tIndirect)) {
        if (add_file_to_triple_indirect(_inodeNumF, _file_name, inode.tIndirect, _allocate))
            return True;
    }
    return False;
}

/**
 * @brief Writes a file to an inode's unset indirect pointer location.
 * Writes a file into the specified inode memory within the File Sector Manager after setting it.
 * @param[in] _type the indirect PointerType (SINGLE, DOUBLE, TRIPLE).
 * @param[in] _inodeNumF Inode number of the file to be written.
 * @param[in] _file_name Pointer to the name of the file.
 * @param[in] _inodeNumD Inode number of the directory to store the file in.
 * @param[in,out] diskOffset Offset within the disk.
 * @param[in] _file_buffer Pointer to the file data buffer.
 * @return True if the file was written successfully, false otherwise.
 * @date 2025-06-17 First implementation.
 */
static Bool create_file_in_unavail_indirect_loc(PointerType _type, unsigned int _inodeNumF,
                                                unsigned int *_file_name, unsigned int _inodeNumD,
                                                unsigned int *diskOffset,
                                                unsigned int *file_buffer) {
    typedef Bool (*WriteFunc)(unsigned int, unsigned int *, unsigned int, Bool);

    unsigned int *indirect = NULL;
    WriteFunc write_func;

    if (_type == SINGLE) {
        indirect = &inode.sIndirect;
        write_func = add_file_to_single_indirect;
    } else if (_type == DOUBLE) {
        indirect = &inode.dIndirect;
        write_func = add_file_to_double_indirect;
    } else {
        indirect = &inode.tIndirect;
        write_func = add_file_to_triple_indirect;
    }

    // Check if memory has been allocated for this indirect memory
    if (is_null(*indirect)) {
        // Allocate memory for the inode's indirect pointer using SSM and update the inode
        *indirect = ssm_allocate_sectors(1);
        if (is_null(*indirect)) {
            return False;
        } else {
            memset(file_buffer, 0xFF, BLOCK_SIZE);
            *diskOffset = *indirect;
            fseek(fsm->diskHandle, 0, SEEK_SET);
            fseek(fsm->diskHandle, *diskOffset, SEEK_SET);

            fwrite(file_buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
            fseek(fsm->diskHandle, 0, SEEK_SET);
            inode_write(&inode, _inodeNumD, fsm->diskHandle);
        }
        // Write data to this indirect memory location on disk
        if (write_func(_inodeNumF, _file_name, *diskOffset, True)) {
            return True;
        }
    }
    return False;
}

/**
 * @brief Writes a file to an inode's set direct pointer location.
 * Writes a file into the specified inode memory within the File Sector Manager after setting it.
 * @param[in] _inodeNumF Inode number of the file to be written.
 * @param[in] _file_name Pointer to the name of the file.
 * @param[in,out] diskOffset Offset within the disk.
 * @param[in] _disk_buffer Pointer to the disk io buffer.
 * @return True if the file was written successfully, false otherwise.
 * @date 2025-06-17 First implementation.
 */
static Bool create_file_in_avail_direct_loc(unsigned int _inodeNumF, unsigned int *_file_name,
                                            unsigned int *diskOffset, unsigned int *disk_buffer) {
    // Read file's direct pointers from disk
    unsigned int j;
    for (unsigned int i = 0; i < INODE_DIRECT_PTRS; i++) {
        if (is_not_null(inode.directPtr[i])) {
            *diskOffset = inode.directPtr[i];
            fseek(fsm->diskHandle, *diskOffset, SEEK_SET);

            fread(disk_buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
            for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                if (disk_buffer[j + 3] == 0) {
                    disk_buffer[j + 3] = 1;
                    disk_buffer[j] = _file_name[0];
                    disk_buffer[j + 1] = _file_name[1];
                    disk_buffer[j + 2] = _inodeNumF;
                    inode.linkCount += 1;
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    fseek(fsm->diskHandle, *diskOffset, SEEK_SET);

                    fwrite(disk_buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    Bool status = fs_close_file();
                    if (status == False) printf("Error closing file\n");
                    return True;
                }  // end if (disk_buffer[j+3] == 0)
            }  // end or (j = 0; j < BLOCK_SIZE/4; j += 4)
        }
    }  // end for (i = 0; i < INODE_DIRECT_PTRS; i++)
    return False;
}

/**
 * @brief Writes a file to an inode's set direct pointer location.
 * Writes a file into the specified inode memory within the File Sector Manager after setting it.
 * @param[in] _inodeNumF Inode number of the file to be written.
 * @param[in] _inodeNumD Inode number of the target directory.
 * @param[in] _file_name Pointer to the name of the file.
 * @param[in,out] diskOffset Offset within the disk.
 * @param[in] _disk_buffer Pointer to the disk io buffer.
 * @return True if the file was written successfully, false otherwise.
 * @date 2025-06-17 First implementation.
 */
static Bool create_file_in_unavail_direct_loc(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                              unsigned int *_file_name, unsigned int *diskOffset,
                                              unsigned int *disk_buffer) {
    unsigned int j;
    for (unsigned int i = 0; i < INODE_DIRECT_PTRS; i++) {
        if (is_null(inode.directPtr[i])) {
            // Get sectors for direct pointers
            *diskOffset = ssm_allocate_sectors(1);
            if (is_null(*diskOffset)) {
                // If sectors can't be retrieved, return false
                return False;
            } else {
                inode.directPtr[i] = *diskOffset;
                fseek(fsm->diskHandle, *diskOffset, SEEK_SET);
                // Clear disk_buffer
                for (j = 0; j < BLOCK_SIZE / 4; j++) {
                    disk_buffer[j] = 0;
                }  // end for (j = 0; j < BLOCK_SIZE/4; j++)
                disk_buffer[3] = 1;
                disk_buffer[0] = _file_name[0];
                disk_buffer[1] = _file_name[1];
                disk_buffer[2] = _inodeNumF;
                inode.linkCount += 1;
                inode.fileSize += BLOCK_SIZE;
                inode.dataBlocks = inode.fileSize / BLOCK_SIZE;

                fwrite(disk_buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                // Write file to disk
                fseek(fsm->diskHandle, 0, SEEK_SET);
                inode_write(&inode, _inodeNumD, fsm->diskHandle);
                Bool status = fs_close_file();
                if (status == False) printf("Error closing file\n");
                return True;
            }
        }
    }  // end for (i = 0; i < INODE_DIRECT_PTRS; i++)
    return False;
}

/**
 * @brief Write a file.
 * Inserts a file into the specified directory within the File Sector Manager.
 * @param[in] _inodeNumF Inode number of the file to be added.
 * @param[in] _name Pointer to the name of the file.
 * @param[in] _inodeNumParentDir Inode number of the target directory.
 * @return True if the file was added successfully, false otherwise.
 * @date 2010-04-01 First implementation.
 */
static Bool create_file(unsigned int _inodeNumF, unsigned int *_name,
                        unsigned int _inodeNumParentDir) {
    unsigned int diskOffset = 0;
    unsigned int buffer[BLOCK_SIZE / 4];
    unsigned int buffer2[BLOCK_SIZE / 4];

    // Open the parent directory
    if (!fs_open_file(_inodeNumParentDir, &inode)) {
        return False;
    }
    // Create file within the parent directory

    // Read file's direct pointers from disk
    if (create_file_in_avail_direct_loc(_inodeNumF, _name, &diskOffset, buffer)) {
        return True;
    }

    // Try to write to the first available indirect memory
    if (create_file_in_avail_indirect_loc(_inodeNumF, _name, False)) {
        return True;
    }

    // If can not find an open Direct pointer, allocate a new one at lowest possible level
    if (create_file_in_unavail_direct_loc(_inodeNumF, _inodeNumParentDir, _name, &diskOffset,
                                          buffer2)) {
        return True;
    }

    // Try to write to the first available indirect memory location
    // @todo revist the need for this call
    if (create_file_in_avail_indirect_loc(_inodeNumF, _name, False)) {
        return True;
    }

    // If can not find an open Single, Double or Triple Indirect pointer,
    // allocate a new one at lowest possible level
    if (create_file_in_avail_indirect_loc(_inodeNumF, _name, True)) {
        return True;
    }

    if (create_file_in_unavail_indirect_loc(SINGLE, _inodeNumF, _name, _inodeNumParentDir,
                                            &diskOffset, buffer2)) {
        return True;
    }

    if (create_file_in_unavail_indirect_loc(DOUBLE, _inodeNumF, _name, _inodeNumParentDir,
                                            &diskOffset, buffer2)) {
        return True;
    }
    if (create_file_in_unavail_indirect_loc(TRIPLE, _inodeNumF, _name, _inodeNumParentDir,
                                            &diskOffset, buffer2)) {
        return True;
    }
    return False;
}

/**
 * @brief Adds a file to a triple indirect pointer.
 * Inserts a file into the structure at the specified triple indirect offset,
 * optionally allocating necessary blocks.
 * @param[in] _inodeNumF Inode number of the file to be added.
 * @param[in] _name Pointer to the name of the file.
 * @param[in] _tIndirectOffset Offset within the triple indirect block.
 * @param[in] _allocate If true, allocate blocks as needed.
 * @return True if the file was added successfully, false otherwise.
 * @date 2010-04-01 First implementation.
 */
static Bool add_file_to_triple_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _tIndirectOffset, Bool _allocate) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int buffer[BLOCK_SIZE / 4];
    unsigned int diskOffset;
    Bool success;
    diskOffset = _tIndirectOffset;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);

    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = add_file_to_double_indirect(_inodeNumF, _name, diskOffset, _allocate);
            if (success == True) {
                return True;
            }  // end if (success == True)
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Allocate more sectors to write to
    if (_allocate == True) {
        for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
            if (is_null(indirectBlock[i])) {
                indirectBlock[i] = ssm_allocate_sectors(1);
                if (is_null(indirectBlock[i])) {
                    return False;
                } else {
                    fseek(fsm->diskHandle, _tIndirectOffset, SEEK_SET);
                    fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                    memset(buffer, 0xFF, BLOCK_SIZE);
                    fseek(fsm->diskHandle, indirectBlock[i], SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                }
                diskOffset = indirectBlock[i];
                success = add_file_to_double_indirect(_inodeNumF, _name, diskOffset, _allocate);
                if (success == True) {
                    return True;
                }  // end if(success == True)
            }  // end else
        }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    }  // end if (_allocate == True)
    return False;
}

/**
 * @brief Adds a file to a double indirect pointer.
 * Inserts a file into the structure at the specified double indirect offset,
 * optionally allocating space as needed.
 * @param[in] _inodeNumF Inode number of the file to be added.
 * @param[in] _name Pointer to the name of the file.
 * @param[in] _dIndirectOffset Offset within the double indirect block.
 * @param[in] _allocate If true, allocate additional space as needed.
 * @return True if the file was added successfully, false otherwise.
 * @date 2010-04-01 First implementation.
 */
static Bool add_file_to_double_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _dIndirectOffset, Bool _allocate) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int buffer[BLOCK_SIZE / 4];
    unsigned int diskOffset;
    Bool success;
    diskOffset = _dIndirectOffset;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);

    // Add file to a double indirect pointer
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = add_file_to_single_indirect(_inodeNumF, _name, diskOffset, _allocate);
            if (success == True) {
                return True;
            }  // end if (success == True)
        }
    }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    if (_allocate == True) {
        for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
            if (is_null(indirectBlock[i])) {
                indirectBlock[i] = ssm_allocate_sectors(1);
                if (is_null(indirectBlock[i])) {
                    return False;
                } else {
                    fseek(fsm->diskHandle, _dIndirectOffset, SEEK_SET);
                    fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                    memset(buffer, 0xFF, BLOCK_SIZE);
                    fseek(fsm->diskHandle, indirectBlock[i], SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                }
                diskOffset = indirectBlock[i];
                success = add_file_to_single_indirect(_inodeNumF, _name, diskOffset, _allocate);
                if (success == True) {
                    return True;
                }  // end if (success == True)
            }
        }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    }  // end if (_allocate == True)
    return False;
}

/**
 * @brief Adds a file to a single indirect pointer without allocation.
 * Inserts a file at the specified single indirect offset, optionally allocating space if required.
 * @param[in] _indirectBlock an indirect block buffer
 * @param[in,out] buffer disk buffer
 * @param[in] _inodeNumF Inode number of the file to be added.
 * @param[in] _name Pointer to the name of the file.
 * @param[in,out] diskOffset Offset within the single indirect block.
 * @return True if the file was added successfully, false otherwise.
 * @date 2010-04-01 First implementation.
 */
static Bool add_file_to_single_indirect_no_alloc(unsigned int *buffer, unsigned int *indirectBlock,
                                                 unsigned int _inodeNumF, unsigned int *_name,
                                                 unsigned int *diskOffset) {
    Bool status = False;
    unsigned int j = 0;
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            *diskOffset = indirectBlock[i];
            fseek(fsm->diskHandle, *diskOffset, SEEK_SET);
            fread(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                if (buffer[j + 3] == 0) {
                    buffer[j + 3] = 1;
                    buffer[j] = _name[0];
                    buffer[j + 1] = _name[1];
                    buffer[j + 2] = _inodeNumF;
                    inode.linkCount += 1;
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    fseek(fsm->diskHandle, *diskOffset, SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    status = fs_close_file();
                    if (status == False) printf("Error closing file\n");
                    return True;
                }  // end if (buffer[j+3] == 0)
            }  // for (j = 0; j < BLOCK_SIZE/4; j += 4)
        }
    }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    return status;
}

/**
 * @brief Adds a file to a single indirect pointer.
 * Inserts a file at the specified single indirect offset, optionally allocating space if required.
 * @param[in] _inodeNumF Inode number of the file to be added.
 * @param[in] _name Pointer to the name of the file.
 * @param[in] _sIndirectOffset Offset within the single indirect block.
 * @param[in] _allocate If true, allocate space as needed.
 * @return True if the file was added successfully, false otherwise.
 * @date 2010-04-01 First implementation.
 */
static Bool add_file_to_single_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                        unsigned int _sIndirectOffset, Bool _allocate) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset;
    unsigned int buffer[BLOCK_SIZE / 4];
    diskOffset = _sIndirectOffset;  // inode.sIndirect;
    Bool status;
    // Read indirect block
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);

    if (_allocate == False) {
        status = add_file_to_single_indirect_no_alloc(buffer, indirectBlock, _inodeNumF, _name,
                                                      &diskOffset);
    }  // if (_allocate == False)
    else {
        // Allocate space then write
        for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
            if (is_null(indirectBlock[i])) {
                indirectBlock[i] = ssm_allocate_sectors(1);
                if (is_null(indirectBlock[i])) {
                    return False;
                } else {
                    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                    fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                    memset(buffer, 0, BLOCK_SIZE);

                    buffer[3] = 1;
                    buffer[0] = _name[0];
                    buffer[1] = _name[1];
                    buffer[2] = _inodeNumF;
                    inode.linkCount += 1;
                    inode.fileSize += BLOCK_SIZE;
                    inode.dataBlocks = inode.fileSize / BLOCK_SIZE;
                    fseek(fsm->diskHandle, indirectBlock[i], SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    status = fs_close_file();
                    if (status == False) printf("Error closing file\n");
                    return True;
                }
            }
        }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    }  // end else
    return False;
}

/**
 * @brief Removes a file from a directory indirect pointers.
 * Removes the file identified by `_inodeNumF`.
 * @param[in] _inodeNumF Inode number of the file to be removed.
 * @param[in] _inodeNumD Inode number of the file's parent directory
 * @return True if the file was successfully removed, false if the file could not be accessed.
 * @date 2010-04-01 First implementation.
 */
static Bool remove_file_from_dir_indirect_pointers(unsigned int _inodeNumF,
                                                   unsigned int _inodeNumD) {
    // if the directory has files in it's sindirect pointer area, remove them
    Bool success = False;
    if (is_not_null(inode.sIndirect)) {
        success = remove_file_from_single_indirect(_inodeNumF, _inodeNumD, (unsigned int)(-1),
                                                   inode.sIndirect);
        if (success == True) {
            return True;
        }  // end if (success == True)
    }
    // if the directory has files in it's dindirect pointer area, remove them
    if (is_not_null(inode.dIndirect)) {
        success = remove_file_from_double_indirect(_inodeNumF, _inodeNumD, (unsigned int)(-1),
                                                   inode.dIndirect);
        if (success == True) {
            return True;
        }  // end if (success == True)
    }
    // if the directory has files in it's tindirect pointer area, remove them
    if (is_not_null(inode.tIndirect)) {
        success = remove_file_from_triple_indirect(_inodeNumF, _inodeNumD, inode.tIndirect);
        if (success == True) {
            return True;
        }  // end if (success == True)
    }
    return success;
}

Bool fs_remove_file_from_dir(unsigned int _inodeNumF, unsigned int _inodeNumD) {
    unsigned int j, k, diskOffset, sectorNum;
    unsigned int buffer[BLOCK_SIZE / 4];
    // open the parent directory inode
    if (!fs_open_file(_inodeNumD, &inode)) return False;
    if (inode.fileType == 2) {  // type 2 is directory
        for (unsigned int i = 0; i < INODE_DIRECT_PTRS; i++) {
            if (is_not_null(inode.directPtr[i])) {
                diskOffset = inode.directPtr[i];
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fread(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                // clear all 4 bytes on each loop
                for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                    if (buffer[j + 3] == 1 && buffer[j + 2] == _inodeNumF) {
                        memset(&buffer[j], 0, 4 * sizeof(unsigned int));
                        inode.linkCount -= 1;
                        if (inode.linkCount == 0) {
                            // if there is nothing in the directory, set size to 0
                            inode.fileSize = 0;
                            if (inode_init_ptrs(&inode) == FAILURE) {
                                // @todo do something here
                            }
                        }
                        fseek(fsm->diskHandle, 0, SEEK_SET);
                        fseek(fsm->diskHandle, diskOffset, SEEK_SET);

                        fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                        for (k = 0; k < BLOCK_SIZE / 4; k += 4) {
                            if (buffer[k + 3] == 1) {
                                break;
                            }
                        }
                        if (k == BLOCK_SIZE / 4) {
                            sectorNum = inode.directPtr[i] / BLOCK_SIZE;
                            ssm_deallocate_sectors(sectorNum);
                            inode.directPtr[i] = (unsigned int)(-1);
                            inode.dataBlocks -= 1;
                            inode_write(&inode, _inodeNumD, fsm->diskHandle);
                        }
                        return True;
                    }
                }
            }
        }
        if (remove_file_from_dir_indirect_pointers(_inodeNumF, _inodeNumD)) {
            return True;
        }
    }
    return False;
}

/**
 * @brief Removes a file from a triple indirect pointer.
 * Removes the file identified by `_inodeNumF` from the triple indirect block at the specified
 * offset. Internally calls `rmFileFrom_D_Indirect` to complete the operation.
 * @param[in] _inodeNumF Inode number of the file to be removed.
 * @param[in] _inodeNumD Inode number of the file's parent.
 * @param[in] _tIndirectOffset Offset to the triple indirect block containing the file.
 * @return True if the file was successfully removed, false if the file could not be accessed.
 * @date 2010-04-01 First implementation.
 */
static Bool remove_file_from_triple_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _tIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset, sectorNum;
    Bool success;
    diskOffset = _tIndirectOffset;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    unsigned int k;
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = remove_file_from_double_indirect(_inodeNumF, _inodeNumD, _tIndirectOffset,
                                                       diskOffset);
            if (success == True) {
                diskOffset = _tIndirectOffset;
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                for (k = 0; k < BLOCK_SIZE / 4; k++) {
                    if (is_not_null(indirectBlock[k])) break;
                }  // end for (k = 0; k < BLOCK_SIZE/4; k++)
                if (k == BLOCK_SIZE / 4) {
                    sectorNum = _tIndirectOffset / BLOCK_SIZE;
                    ssm_deallocate_sectors(sectorNum);
                    inode.tIndirect = (unsigned int)(-1);
                    inode_write(&inode, _inodeNumD, fsm->diskHandle);
                }  // end if (k == BLOCK_SIZE/4)
                return True;
            }  // if (success == True)
        }
    }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    return False;
}

/**
 * @brief Removes a file from a double indirect pointer.
 * Removes the file identified by `_inodeNumF` from the double indirect block at the specified
 * offset. Typically called by higher-level triple indirect removal logic.
 * @param[in] _inodeNumF Inode number of the file to be removed.
 * @param[in] _inodeNumD Inode number of the file's parent.
 * @param[in] _tIndirectOffset Offset to the parent triple indirect block.
 * @param[in] _dIndirectOffset Offset to the double indirect block containing the file.
 * @return True if the file was successfully removed, false if the file could not be accessed.
 * @date 2010-04-01 First implementation.
 */
static Bool remove_file_from_double_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _tIndirectOffset,
                                             unsigned int _dIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset, sectorNum;
    Bool success;
    // load next pointer
    diskOffset = _dIndirectOffset;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    unsigned int k;
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = remove_file_from_single_indirect(_inodeNumF, _inodeNumD, _dIndirectOffset,
                                                       diskOffset);
            if (success == True) {
                diskOffset = _dIndirectOffset;
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                for (k = 0; k < BLOCK_SIZE / 4; k++) {
                    if (is_not_null(indirectBlock[k])) break;
                }  // end for (k = 0; k < BLOCK_SIZE/4; k++)
                if (k == BLOCK_SIZE / 4) {
                    sectorNum = _dIndirectOffset / BLOCK_SIZE;
                    ssm_deallocate_sectors(sectorNum);
                    if (is_not_null(_tIndirectOffset)) {
                        diskOffset = _tIndirectOffset;
                        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                        fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                        for (k = 0; k < BLOCK_SIZE / 4; k++) {
                            if (indirectBlock[k] == _dIndirectOffset) {
                                indirectBlock[k] = (unsigned int)(-1);
                                break;
                            }  // end if (indirectBlock[k] == _dIndirectOffset)
                        }  // end for (k = 0; k < BLOCK_SIZE/4; k++)
                        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                        fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                    } else {
                        inode.dIndirect = (unsigned int)(-1);
                        inode_write(&inode, _inodeNumD, fsm->diskHandle);
                    }  // end else
                }  // if (k == BLOCK_SIZE/4)
                return True;
            }  // end if (success == True)
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    return False;
}

/**
 * @brief Removes a file from a single indirect pointer.
 * Removes the file identified by `_inodeNumF` from the single indirect block located
 * via the specified double and single indirect offsets.
 * @param[in] _inodeNumF Inode number of the file to be removed.
 * @param[in] _inodeNumD Inode number of the file's parent.
 * @param[in] _dIndirectOffset Offset to the parent double indirect block.
 * @param[in] _sIndirectOffset Offset to the single indirect block containing the file.
 * @return True if the file was successfully removed, false if the file could not be accessed.
 * @date 2010-04-01 First implementation.
 */
static Bool remove_file_from_single_indirect(unsigned int _inodeNumF, unsigned int _inodeNumD,
                                             unsigned int _dIndirectOffset,
                                             unsigned int _sIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset, sectorNum, j, k;
    unsigned int buffer[BLOCK_SIZE / 4];
    diskOffset = _sIndirectOffset;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fread(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                if (buffer[j + 3] == 1 && buffer[j + 2] == _inodeNumF) {
                    memset(&buffer[j], 0, 4 * sizeof(unsigned int));
                    inode.linkCount -= 1;
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    fseek(fsm->diskHandle, diskOffset, SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    for (k = 0; k < BLOCK_SIZE / 4; k += 4) {
                        if (buffer[k + 3] == 1) break;
                    }
                    if (k == BLOCK_SIZE / 4) {
                        sectorNum = indirectBlock[i] / BLOCK_SIZE;
                        ssm_deallocate_sectors(sectorNum);
                        inode.dataBlocks -= 1;
                        inode_write(&inode, _inodeNumD, fsm->diskHandle);
                        indirectBlock[i] = (unsigned int)(-1);
                        diskOffset = _sIndirectOffset;
                        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                        fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                        for (k = 0; k < BLOCK_SIZE / 4; k++) {
                            if (is_not_null(indirectBlock[k])) break;
                        }
                        if (k == BLOCK_SIZE / 4) {
                            sectorNum = _sIndirectOffset / BLOCK_SIZE;
                            ssm_deallocate_sectors(sectorNum);
                            if (is_not_null(_dIndirectOffset)) {
                                diskOffset = _dIndirectOffset;
                                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                                fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                                for (k = 0; k < BLOCK_SIZE / 4; k++) {
                                    if (indirectBlock[k] == _sIndirectOffset) {
                                        indirectBlock[k] = (unsigned int)(-1);
                                        break;
                                    }
                                }
                                fseek(fsm->diskHandle, diskOffset, SEEK_SET);

                                fwrite(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
                            } else {
                                inode.sIndirect = (unsigned int)(-1);
                                inode_write(&inode, _inodeNumD, fsm->diskHandle);
                            }
                        }
                    }
                    return True;
                }
            }
        }
    }
    return False;
}

/**
 * @brief Reads pointers from a triple indirect block into a buffer.
 * Fills the provided buffer with all pointers from a specified triple indirect block,
 * one block at a time. Internally calls `readFrom_D_IndirectBlocks`.
 * @param[out] _buffer Pointer to the buffer where the read data will be stored.
 * @param[in] _diskOffset Offset to the first usable block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void read_from_triple_indirect_blocks(void *_buffer, unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    void *buffer = _buffer;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            read_from_double_indirect_blocks(buffer, diskOffset);
            buffer = (char *)buffer + BLOCK_SIZE * D_INDIRECT_BLOCKS;
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
}

/**
 * @brief Reads pointers from a double indirect block into a buffer.
 * Fills the provided buffer with all pointers from a specified double indirect block,
 * one block at a time. Internally calls `readFrom_S_IndirectBlocks`.
 * @param[out] _buffer Pointer to the buffer where the read data will be stored.
 * @param[in] _diskOffset Offset to the first usable block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void read_from_double_indirect_blocks(void *_buffer, unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    void *buffer = _buffer;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            read_from_single_indirect_blocks(buffer, diskOffset);
            buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
}

/**
 * @brief Reads pointers from a single indirect block into a buffer.
 * Fills the provided buffer with all pointers from a specified single indirect block,
 * one block at a time.
 * @param[out] _buffer Pointer to the buffer where the read data will be stored.
 * @param[in] _diskOffset Offset to the first usable block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void read_from_single_indirect_blocks(void *_buffer, unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    void *buffer = _buffer;
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fread(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            buffer = (char *)buffer + BLOCK_SIZE;
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
}

/**
 * @brief Allocates a triple indirect block and all underlying levels of pointers.
 * Allocates sectors via the SSM to form a triple indirect structure:
 * a block pointing to blocks of pointers, which in turn point to more pointer blocks.
 * Returns the address of the top-level triple indirect block.
 * @param[in] _blockCount Number of blocks to allocate.
 * @return The address of the triple indirect block on success, or -1 on failure.
 * @date 2010-04-12 First implementation.
 */
static unsigned int aloc_triple_indirect(long long int _blockCount) {
    unsigned int baseAddress, address, diskOffset;
    long long int blockCount = _blockCount;
    baseAddress = ssm_allocate_sectors(1);
    if (is_not_null(baseAddress)) {
        diskOffset = baseAddress;  // inode.tIndirect;
        // Initialize the indirect block pointers to -1
        unsigned int base[BLOCK_SIZE / 4];
        memset(base, 0xFF, BLOCK_SIZE);
        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
        fwrite(base, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
        // Allocate _blockCount blocks and store their pointers in
        // the indirect block
        for (unsigned int i = 0; i < PTRS_PER_BLOCK; i++) {
            address = aloc_double_indirect(_blockCount);
            // write to buffer
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fwrite(&address, sizeof(unsigned int), 1, fsm->diskHandle);
            diskOffset += 4;
            // update block count
            blockCount -= D_INDIRECT_BLOCKS;
            if (blockCount < 0) {
                break;
            }  // end if (blockCount < 0)
        }  // end for (i = 0; i < PTRS_PER_BLOCK; i++)
        // return the address of the tindirect block
        return baseAddress;
    } else {
        return (unsigned int)(-1);
    }  // end else
}

/**
 * @brief Allocates a double indirect block and its underlying pointer blocks.
 * Allocates sectors via the SSM to form a double indirect structure:
 * a block pointing to blocks of pointers, which in turn point to data blocks.
 * Returns the address of the top-level double indirect block.
 * @param[in] _blockCount Number of blocks to allocate.
 * @return The address of the double indirect block on success, or -1 on failure.
 * @date 2010-04-12 First implementation.
 */
static unsigned int aloc_double_indirect(long long int _blockCount) {
    unsigned int baseAddress, address, diskOffset;
    long long int blockCount = _blockCount;
    // calculate base address
    baseAddress = ssm_allocate_sectors(1);
    if (is_not_null(baseAddress)) {
        diskOffset = baseAddress;  // inode.sIndirect;
        // Initialize the indirect block pointers to -1
        unsigned int base[BLOCK_SIZE / 4];
        memset(base, 0xFF, BLOCK_SIZE);
        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
        fwrite(base, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
        // Allocate _blockCount blocks and store their pointers in
        // the indirect block
        for (unsigned int i = 0; i < PTRS_PER_BLOCK; i++) {
            address = aloc_single_indirect(_blockCount);
            // write to buffer
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fwrite(&address, sizeof(unsigned int), 1, fsm->diskHandle);
            diskOffset += 4;
            // update block count
            blockCount -= S_INDIRECT_BLOCKS;
            if (blockCount < 0) {
                break;
            }  // end if (blockCount < 0
        }  // end for (i = 0; i < PTRS_PER_BLOCK; i++)
        // return the address of the Dindirect block
        return baseAddress;
    } else {
        return (unsigned int)(-1);
    }  // end else
}

/**
 * @brief Allocates a single indirect block and its associated data blocks.
 * Allocates a sector from the SSM, fills it with pointers to other sectors,
 * and returns the address of the allocated single indirect block.
 * @param[in] _blockCount Number of blocks to allocate.
 * @return The address of the single indirect block on success, or -1 on failure.
 * @date 2010-04-12 First implementation.
 */
static unsigned int aloc_single_indirect(long long int _blockCount) {
    unsigned int baseAddress, address, diskOffset;
    // calculate base address
    baseAddress = ssm_allocate_sectors(1);
    if (is_not_null(baseAddress)) {
        diskOffset = baseAddress;  // inode.sIndirect;
        // Initialize the indirect block pointers to -1
        unsigned int base[BLOCK_SIZE / 4];
        memset(base, 0xFF, BLOCK_SIZE);
        fseek(fsm->diskHandle, diskOffset, SEEK_SET);
        fwrite(base, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
        // Allocate _blockCount blocks and store associated pointers in
        // the indirect block
        for (unsigned int i = 0; i < _blockCount && i < PTRS_PER_BLOCK; i++) {
            address = ssm_allocate_sectors(1);
            if (is_not_null(address)) {
                // write to buffer
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fwrite(&address, sizeof(unsigned int), 1, fsm->diskHandle);
                diskOffset += 4;
            }
        }  // end for (i = 0; i < _blockCount && i < PTRS_PER_BLOCK; i++
        // return the address of the sindirect block
        return baseAddress;
    } else {
        return (unsigned int)(-1);
    }  // end else
}

/**
 * @brief Writes data into triple indirect blocks one block at a time.
 * Writes the contents of the given buffer into a triple indirect structure,
 * starting at the specified base offset. Internally calls `writeTo_D_IndirectBlocks`
 * to handle the lower levels.
 * @param[in] _baseOffset Offset to the first usable data block on disk.
 * @param[in] _buffer Pointer to the buffer containing data to be written.
 * @param[in] _tIndirectPtrs Number of blocks to allocate through the triple indirect pointer.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void write_to_triple_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _tIndirectPtrs) {
    unsigned int diskOffset, tIndirectPtrs;
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    void *buffer = _buffer;
    tIndirectPtrs = _tIndirectPtrs;
    fseek(fsm->diskHandle, _baseOffset, SEEK_SET);
    fread(&indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // write to each of the sIndirectPtrs blocks
    for (unsigned int i = 0; i < PTRS_PER_BLOCK; i++) {
        diskOffset = indirectBlock[i];
        if (is_null(diskOffset)) {
            break;
        } else {
            if (tIndirectPtrs - D_INDIRECT_BLOCKS > 0) {
                // write a dindirect worth of blocks then continue looping
                write_to_double_indirect_blocks(diskOffset, buffer, D_INDIRECT_BLOCKS);
                buffer = (char *)buffer + BLOCK_SIZE * D_INDIRECT_BLOCKS;
                tIndirectPtrs -= D_INDIRECT_BLOCKS;
            }  // end if (tIndirectPtrs - D_INDIRECT_BLOCKS > 0)
            else if (tIndirectPtrs - D_INDIRECT_BLOCKS == 0) {
                // write the rest into a dindirect block
                write_to_double_indirect_blocks(diskOffset, buffer, D_INDIRECT_BLOCKS);
                buffer = (char *)buffer + BLOCK_SIZE * D_INDIRECT_BLOCKS;
                tIndirectPtrs -= D_INDIRECT_BLOCKS;
                break;
            } else {
                // write the rest into a dindirect block
                write_to_double_indirect_blocks(diskOffset, buffer, tIndirectPtrs);
                buffer = (char *)buffer + BLOCK_SIZE * tIndirectPtrs;
                tIndirectPtrs = 0;
                break;
            }  // end else
        }  // end else
    }  // end for (i = 0; i < PTRS_PER_BLOCK; i++)
}

/**
 * @brief Writes data into double indirect blocks one block at a time.
 * Writes the contents of the given buffer into a double indirect structure,
 * starting at the specified base offset. Internally calls `writeTo_S_IndirectBlocks`
 * to handle the single indirect level.
 * @param[in] _baseOffset Offset to the first usable data block on disk.
 * @param[in] _buffer Pointer to the buffer containing data to be written.
 * @param[in] _dIndirectPtrs Number of blocks to allocate through the double indirect pointer.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void write_to_double_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _dIndirectPtrs) {
    unsigned int diskOffset;
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    void *buffer = _buffer;
    unsigned int dIndirectPtrs = _dIndirectPtrs;
    fseek(fsm->diskHandle, _baseOffset, SEEK_SET);
    fread(&indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // write to each of the sIndirectPtrs blocks
    for (unsigned int i = 0; i < PTRS_PER_BLOCK; i++) {
        diskOffset = indirectBlock[i];
        if (is_null(diskOffset)) {
            break;
        } else {
            // write a sindirect block at a time
            if (dIndirectPtrs - S_INDIRECT_BLOCKS > 0) {
                write_to_single_indirect_blocks(diskOffset, buffer, S_INDIRECT_BLOCKS);
                buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
                dIndirectPtrs -= S_INDIRECT_BLOCKS;
            }  // end if (dIndirectPtrs - S_INDIRECT_BLOCKS > 0)
            // write the rest into a sindirect block
            else if (dIndirectPtrs - S_INDIRECT_BLOCKS == 0) {
                write_to_single_indirect_blocks(diskOffset, buffer, S_INDIRECT_BLOCKS);
                buffer = (char *)buffer + BLOCK_SIZE * S_INDIRECT_BLOCKS;
                dIndirectPtrs -= S_INDIRECT_BLOCKS;
                break;
            }  // end else if (dIndirectPtrs - S_INDIRECT_BLOCKS == 0)
            // write the rest into a sindirect block
            else {
                write_to_single_indirect_blocks(diskOffset, buffer, dIndirectPtrs);
                buffer = (char *)buffer + BLOCK_SIZE * dIndirectPtrs;
                dIndirectPtrs = 0;
                break;
            }  // end else
        }  // end else
    }  // end for (i = 0; i < PTRS_PER_BLOCK; i++)
}

/**
 * @brief Writes data into single indirect blocks one block at a time.
 * Writes the contents of the provided buffer into a single indirect structure,
 * starting at the specified base offset.
 * @param[in] _baseOffset Offset to the first usable data block on disk.
 * @param[in] _buffer Pointer to the buffer containing data to be written.
 * @param[in] _sIndirectPtrs Number of blocks to allocate through the single indirect pointer.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void write_to_single_indirect_blocks(unsigned int _baseOffset, void *_buffer,
                                            unsigned int _sIndirectPtrs) {
    unsigned int diskOffset;
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    void *buffer = _buffer;
    // read the single indirect pointer
    fseek(fsm->diskHandle, _baseOffset, SEEK_SET);
    fread(&indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // write to each of the sIndirectPtrs blocks
    for (unsigned int i = 0; i < _sIndirectPtrs; i++) {
        // get next pointer
        diskOffset = indirectBlock[i];
        // if next pointer is unused, break out of loop
        if (is_null(diskOffset)) {
            break;
        } else {
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fwrite(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            buffer = (char *)buffer + BLOCK_SIZE;  // increment buffer
        }  // end else
    }  // end for (i = 0; i < _sIndirectPtrs; i++)
}

Bool fs_remove_file(unsigned int _inodeNum, unsigned int _inodeNumD) {
    // Open file at Inode _inodeNum for reading
    if (!fs_open_file(_inodeNum, &inode)) {
        return False;
    }  // end if (success == False)
    unsigned int directPtrs[INODE_DIRECT_PTRS];
    unsigned int sIndirect = inode.sIndirect;
    unsigned int dIndirect = inode.dIndirect;
    unsigned int tIndirect = inode.tIndirect;
    unsigned int fileType = inode.fileType;
    unsigned int buffer[BLOCK_SIZE / 4];
    unsigned int sectorNumber, j, diskOffset;
    memcpy(directPtrs, inode.directPtr, INODE_DIRECT_PTRS * sizeof(unsigned int));
    // Read data from direct pointers into buffer _buffer
    for (unsigned int i = 0; i < INODE_DIRECT_PTRS; i++) {
        diskOffset = directPtrs[i];
        if (is_not_null(diskOffset)) {
            if (fileType == 2) {
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fread(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                for (j = 8; j < BLOCK_SIZE / 4; j += 4) {
                    if (buffer[j + 3] == 1) {
                        fs_remove_file(buffer[j + 2], _inodeNum);
                    }  // end if (buffer[j+3] == 1)
                }  // end for (j = 8; j < BLOCK_SIZE/4; j += 4)
            }  // end if (fileType == 2)
            sectorNumber = diskOffset / BLOCK_SIZE;
            ssm_deallocate_sectors(sectorNumber);
        }
    }  // end for (i = 0; i < 10; i++)

    // Read data from single indirect pointer into buffer _buffer
    remove_file_indirect_blocks(SINGLE, sIndirect, fileType, _inodeNum, _inodeNumD);
    // Read data from double indirect pointer into buffer _buffer
    remove_file_indirect_blocks(DOUBLE, dIndirect, fileType, _inodeNum, _inodeNumD);
    // Read data from triple indirect pointer into buffer _buffer
    remove_file_indirect_blocks(TRIPLE, tIndirect, fileType, _inodeNum, _inodeNumD);

    // Open inode, assign to success whether opening worked. If inode didn't open, return false
    if (!fs_open_file(_inodeNum, &inode)) {
        return False;
    }  // end if (success == False)
    // Clear the inode
    if (inode_init(&inode) == FAILURE) {
        //@todo do something here
    }
    // Write over inode
    fseek(fsm->diskHandle, 0, SEEK_SET);
    inode_write(&inode, _inodeNum, fsm->diskHandle);
    deallocate_inode(_inodeNum);
    // Remove the file from its containing directory
    fs_remove_file_from_dir(_inodeNum, _inodeNumD);
    // Succeeded so return true
    return True;
}

/**
 * @brief Removes indirect pointers associated with a file.
 * Clears indirect block references for a file, starting at the specified disk offset.
 * @param[in] _type the indirect PointerType (SINGLE, DOUBLE, TRIPLE)
 * @param[in] indirect Offset to the first usable data block on disk.
 * @param[in] _fileType Type of the file being removed.
 * @param[in] _inodeNum Inode number of the file.
 * @param[in] _inodeNumD Inode number of the directory containing the file.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static inline void remove_file_indirect_blocks(PointerType _type, unsigned int indirect,
                                               unsigned int fileType, unsigned int _inodeNum,
                                               unsigned int _inodeNumD) {
    typedef void (*RemoveFunc)(unsigned int, unsigned int, unsigned int);

    RemoveFunc remove_func;

    if (_type == SINGLE) {
        remove_func = remove_file_single_indirect_blocks;
    } else if (_type == DOUBLE) {
        remove_func = remove_file_double_indirect_blocks;
    } else {
        remove_func = remove_file_triple_indirect_blocks;
    }

    // Read data from single indirect pointer into buffer _buffer
    unsigned int diskOffset = indirect;
    if (is_not_null(diskOffset)) {
        if (fileType == 1) {
            // If remove directory, pass inodeNumD
            remove_func(fileType, _inodeNumD, diskOffset);
        } else if (fileType == 2) {
            // If remove file, pass inodeNum
            remove_func(fileType, _inodeNum, diskOffset);
        }
    }
}

/**
 * @brief Removes triple indirect pointers associated with a file.
 * Clears triple indirect block references for a file, starting at the specified disk offset.
 * @param[in] _fileType Type of the file being removed.
 * @param[in] _inodeNumD Inode number of the directory containing the file.
 * @param[in] _diskOffset Offset to the first usable data block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void remove_file_triple_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // Deallocate Double indirect blocks
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            remove_file_double_indirect_blocks(_fileType, _inodeNumD, diskOffset);
        }
    }  // for (i = 0; i < BLOCK_SIZE/4; i++)
    // Deallocate T indirect block
    diskOffset = _diskOffset;
    unsigned int sectorNumber = diskOffset / BLOCK_SIZE;
    ssm_deallocate_sectors(sectorNumber);
}

/**
 * @brief Removes double indirect pointers associated with a file.
 * Clears double indirect block references for a file, starting at the specified disk offset.
 * @param[in] _fileType Type of the file being removed.
 * @param[in] _inodeNumD Inode number of the directory containing the file.
 * @param[in] _diskOffset Offset to the first usable data block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void remove_file_double_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // Remove all the Single indirect pointers
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            remove_file_single_indirect_blocks(_fileType, _inodeNumD, diskOffset);
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Deallocate D indirect block
    diskOffset = _diskOffset;
    unsigned int sectorNumber = diskOffset / BLOCK_SIZE;
    ssm_deallocate_sectors(sectorNumber);
}

/**
 * @brief Removes single indirect pointers associated with a file.
 * Clears single indirect block references for a file, starting at the specified disk offset.
 * @param[in] _fileType Type of the file being removed.
 * @param[in] _inodeNumD Inode number of the directory containing the file.
 * @param[in] _diskOffset Offset to the first usable data block on disk.
 * @return void
 * @date 2010-04-12 First implementation.
 */
static void remove_file_single_indirect_blocks(unsigned int _fileType, unsigned int _inodeNumD,
                                               unsigned int _diskOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _diskOffset;
    unsigned int buffer[BLOCK_SIZE / 4];
    unsigned int sectorNumber, j;
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // Deallocate all Single Indirect Blocks
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            // Deallocate Data Blocks
            if (_fileType == 2) {
                fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                fread(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                for (j = 8; j < BLOCK_SIZE / 4; j += 4) {
                    if (buffer[j + 3] == 1) {
                        fs_remove_file(buffer[j + 2], _inodeNumD);
                    }  // end if (buffer[j+3] == 1)
                }  // end for (j = 8; j < BLOCK_SIZE/4; j += 4)
            }  // end if (_fileType == 2)
            sectorNumber = diskOffset / BLOCK_SIZE;
            ssm_deallocate_sectors(sectorNumber);
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Deallocate S indirect block
    diskOffset = _diskOffset;
    sectorNumber = diskOffset / BLOCK_SIZE;
    ssm_deallocate_sectors(sectorNumber);
}

Bool fs_rename_file(unsigned int _inodeNumF, unsigned int *_name, unsigned int _inodeNumD) {
    Bool success;
    unsigned int j, diskOffset;
    unsigned int buffer[BLOCK_SIZE / 4];
    // Attempt to open directory
    if (fs_open_file(_inodeNumD, &inode)) {
        if (inode.fileType == 2) {
            for (unsigned int i = 0; i < 10; i++) {
                if (is_not_null(inode.directPtr[i])) {
                    diskOffset = inode.directPtr[i];
                    fseek(fsm->diskHandle, diskOffset, SEEK_SET);

                    fread(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                        if (buffer[j + 3] == 1 && buffer[j + 2] == _inodeNumF) {
                            buffer[j] = _name[0];
                            buffer[j + 1] = _name[1];
                            fseek(fsm->diskHandle, 0, SEEK_SET);
                            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
                            fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                            return True;
                        }  // end if (buffer[j+3] == 1 && buffer[j+2] == _inodeNumF)
                    }  // end for (j = 0; j < BLOCK_SIZE/4; j += 4)
                }
            }  // end for (i = 0; i < 10; i++)
            // Rename all the Single Indirect blocks
            if (is_not_null(inode.sIndirect)) {
                success = rename_file_in_single_indirect(_inodeNumF, _name, inode.sIndirect);
                if (success == True) {
                    return True;
                }  // end if (success == True)
            }
            // Rename all Double Indirect block references
            if (is_not_null(inode.dIndirect)) {
                success = rename_file_in_double_indirect(_inodeNumF, _name, inode.dIndirect);
                if (success == True) {
                    return True;
                }  // end if (success == True)
            }
            // Rename all Triple Indirect block references
            if (is_not_null(inode.tIndirect)) {
                success = rename_file_in_triple_indirect(_inodeNumF, _name, inode.tIndirect);
                if (success == True) {
                    return True;
                }  // end if (success == True)
            }
        }  // end if (inode.fileType == 2)
    }  // end if (success == True)
    // Returns false if none of the renames successful
    return False;
}

/**
 * @brief Renames a file within a triple indirect block.
 * Renames the file identified by `_inodeNumF` in the structure pointed to
 * by the triple indirect pointer at `_tIndirectOffset`.
 * @param[in] _inodeNumF Inode number of the file to rename.
 * @param[in] _name Pointer to the new name for the file.
 * @param[in] _tIndirectOffset Offset of the triple indirect pointer.
 * @return True if the rename was successful, false otherwise.
 * @date 2010-04-12 First implementation.
 */
static Bool rename_file_in_triple_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _tIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _tIndirectOffset;
    Bool success;
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // Loop through indirect block, follow the Double indirect pointers
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = rename_file_in_double_indirect(_inodeNumF, _name, diskOffset);
            if (success == True) {
                return True;
            }  // end if (success == True)
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Return false if not successful in renaming
    return False;
}

/**
 * @brief Renames a file within a double indirect block.
 * Renames the file identified by `_inodeNumF` in the structure pointed to
 * by the double indirect pointer at `_dIndirectOffset`.
 * @param[in] _inodeNumF Inode number of the file to rename.
 * @param[in] _name Pointer to the new name for the file.
 * @param[in] _dIndirectOffset Offset of the double indirect pointer.
 * @return True if the rename was successful, false otherwise.
 * @date 2010-04-12 First implementation.
 */
static Bool rename_file_in_double_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _dIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _dIndirectOffset;
    Bool success;
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    // Loop through indirect block, follow the Single indirect pointers
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            success = rename_file_in_single_indirect(_inodeNumF, _name, diskOffset);
            if (success == True) {
                return True;
            }  // end if (success == True)
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Return false if renaming didn't work
    return False;
}

/**
 * @brief Renames a file within a single indirect block.
 * Renames the file identified by `_inodeNumF` in the structure pointed to
 * by the single indirect pointer at `_sIndirectOffset`.
 * @param[in] _inodeNumF Inode number of the file to rename.
 * @param[in] _name Pointer to the new name for the file.
 * @param[in] _sIndirectOffset Offset of the single indirect pointer.
 * @return True if the rename was successful, false otherwise.
 * @date 2010-04-12 First implementation.
 */
static Bool rename_file_in_single_indirect(unsigned int _inodeNumF, unsigned int *_name,
                                           unsigned int _sIndirectOffset) {
    unsigned int indirectBlock[BLOCK_SIZE / 4];
    unsigned int diskOffset = _sIndirectOffset;
    unsigned int buffer[BLOCK_SIZE / 4];
    // Read disk to indirectBlock
    fseek(fsm->diskHandle, diskOffset, SEEK_SET);
    fread(indirectBlock, BLOCK_SIZE, 1, fsm->diskHandle);
    unsigned int j;
    // Loop through indirect blocks
    for (unsigned int i = 0; i < BLOCK_SIZE / 4; i++) {
        if (is_not_null(indirectBlock[i])) {
            diskOffset = indirectBlock[i];
            fseek(fsm->diskHandle, diskOffset, SEEK_SET);
            fread(buffer, BLOCK_SIZE, 1, fsm->diskHandle);
            // Go through buffer, find name portion
            for (j = 0; j < BLOCK_SIZE / 4; j += 4) {
                if (buffer[j + 3] == 1 && buffer[j + 2] == _inodeNumF) {
                    // Write name to data
                    buffer[j] = _name[0];
                    buffer[j + 1] = _name[1];
                    fseek(fsm->diskHandle, 0, SEEK_SET);
                    fseek(fsm->diskHandle, diskOffset, SEEK_SET);

                    fwrite(buffer, sizeof(unsigned int), BLOCK_SIZE / 4, fsm->diskHandle);
                    return True;
                }  // end if (buffer[j+3] == 1 && buffer[j+2] == _inodeNumF)
            }  // end for (j = 0; j < BLOCK_SIZE/4; j += 4)
        }
    }  // end for (i = 0; i < BLOCK_SIZE/4; i++)
    // Return false if renaming not successful
    return False;
}

Bool fs_make(unsigned int _DISK_SIZE, unsigned int _BLOCK_SIZE, unsigned int _INODE_SIZE,
             unsigned int _INODE_BLOCKS, unsigned int _INODE_COUNT, int _initSsmMaps) {
    init_fsm_constants(_DISK_SIZE, _BLOCK_SIZE, _INODE_SIZE, _INODE_BLOCKS, _INODE_COUNT);
    init_fsm_maps();
    init_file_sector_mgr(_initSsmMaps);
    // Allocate the boot and super block sectors on disk
    ssm_allocate_sectors(2);
    // Create INODE_COUNT Inodes
    int factorsOf_32 = INODE_BLOCKS / 32;
    // int remainder = INODE_BLOCKS % 32;
    // get 32 sectors at a time and make them inode sectors
    unsigned int diskOffset;
    for (int i = 0; i < factorsOf_32; i++) {
        diskOffset = ssm_allocate_sectors(32);
        // take the 32 sectors and make inodes
        inode_make(32, fsm->diskHandle, diskOffset);
    }  // end for (i = 0; i < factorsOf_32; i++)
    unsigned int name[2];
    // Set inode 0 for boot sector
    fs_create_file(0, name, (unsigned int)(-1));
    if (!fs_open_file(0, &inode))
        printf("Corruption during file system creation");  // Failed to open inode 0
    inode.directPtr[0] = 0;
    inode_write(&inode, 0, fsm->diskHandle);
    // Set inode 1 for super block
    fs_create_file(0, name, (unsigned int)(-1));
    if (!fs_open_file(1, &inode))
        printf("Corruption during file system creation");  // Failed to open inode 0
    inode.directPtr[0] = BLOCK_SIZE * (1);
    inode_write(&inode, 1, fsm->diskHandle);
    // make root directory with inode 2
    fs_create_file(1, name, (unsigned int)(-1));
    return True;
}

Bool fs_remove(void) {
    if (fsm->diskHandle) {
        fclose(fsm->diskHandle);
        fsm->diskHandle = Null;
    }
    return True;
}
