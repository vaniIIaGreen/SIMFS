/*
*Name: Julio Alvarenga
*Project 1
*12/1/18
*/
#include "simfs.h"

//////////////////////////////////////////////////////////////////////////
//
// allocation of the in-memory data structures
//
//////////////////////////////////////////////////////////////////////////

SIMFS_CONTEXT_TYPE *simfsContext; // all in-memory information about the system
SIMFS_VOLUME *simfsVolume;

//////////////////////////////////////////////////////////////////////////
//
// simfs function implementations
//
//////////////////////////////////////////////////////////////////////////

/*
 * Retuns a hash value within the limits of the directory.
 */
inline unsigned long hash(unsigned char *str)
{
    register unsigned long hash = 5381;
    register unsigned char c;

    while ((c = *str++) != '\0')
        hash = ((hash << 5) + hash) ^ c; /* hash * 33 + c */

    return hash % SIMFS_DIRECTORY_SIZE;
}

/*
 * Find a free block in a bit vector.
 */
inline unsigned short simfsFindFreeBlock(unsigned char *bitvector)
{
    unsigned short i = 0;
    //while the bitector isn't full
    while (bitvector[i] == 0xFF) //0xFF is a full bitvector
        i += 1;

    register unsigned char mask = 0x80; //is 10000000
    unsigned short j = 0;
    while (bitvector[i] & mask){
        mask = mask >> 1;
        j++;
    }

    return (i * 8) + j; // i bytes and j bits are all "1", so this formula points to the first "0"
}

/*
 * Three functions for bit manipulation.
 */
inline void simfsFlipBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] ^= (mask >> bitShift);
    //printf("Bit %hu Flipped\n", bitShift);
}

inline void simfsSetBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] |= (mask >> bitShift);
}

inline void simfsClearBit(unsigned char *bitvector, unsigned short bitIndex)
{
    unsigned short blockIndex = bitIndex / 8;
    unsigned short bitShift = bitIndex % 8;

    register unsigned char mask = 0x80;
    bitvector[blockIndex] &= ~(mask >> bitShift);
}

/*
 * Allocates space for the file system and saves it to disk.
 */
SIMFS_ERROR simfsCreateFileSystem(char *simfsFileName)
{

    FILE *file = fopen(simfsFileName, "wb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    simfsContext = malloc(sizeof(SIMFS_CONTEXT_TYPE));
    if (simfsContext == NULL)
        return SIMFS_ALLOC_ERROR;

    simfsVolume = malloc(sizeof(SIMFS_VOLUME));
    if (simfsVolume == NULL)
        return SIMFS_ALLOC_ERROR;

    // initialize the superblock

    simfsVolume->superblock.rootNodeIndex = 0;
    simfsVolume->superblock.blockSize = SIMFS_BLOCK_SIZE;
    simfsVolume->superblock.numberOfBlocks = SIMFS_NUMBER_OF_BLOCKS;

    // initialize the blocks holding the root folder

    // initialize the root folder

    simfsVolume->block[0].type = FOLDER_CONTENT_TYPE;
    simfsVolume->block[0].content.fileDescriptor.type = FOLDER_CONTENT_TYPE;
    strcpy(simfsVolume->block[0].content.fileDescriptor.name, "/");
    simfsVolume->block[0].content.fileDescriptor.accessRights = 0777; //arbitrary umask to allow for complete
    simfsVolume->block[0].content.fileDescriptor.owner = 0; // arbitrarily simulated
    simfsVolume->block[0].content.fileDescriptor.size = 0;

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    simfsVolume->block[0].content.fileDescriptor.creationTime = time.tv_sec;
    simfsVolume->block[0].content.fileDescriptor.lastAccessTime = time.tv_sec;
    simfsVolume->block[0].content.fileDescriptor.lastModificationTime = time.tv_sec;

    // initialize the index block of the root folder

    // first, point from the root file descriptor to the index block
    simfsVolume->block[0].content.fileDescriptor.block_ref = 1;

    simfsVolume->block[1].type = INDEX_CONTENT_TYPE;

    // indicate that the blocks #0 and #1 are allocated

    simfsFlipBit(simfsVolume->bitvector, simfsFindFreeBlock(simfsVolume->bitvector)); // should be 0
    simfsFlipBit(simfsVolume->bitvector, simfsFindFreeBlock(simfsVolume->bitvector)); // should be 1

    // sample alternative #1 - illustration of bit-wise operations
//    simfsVolume->bitvector[0] = 0;
//    simfsVolume->bitvector[0] |= 0x01 << 7; // set the first bit of the bit vector
//    simfsVolume->bitvector[0] += 0x80 >> 1; // flip the first bit of the bit vector

    // sample alternative #2 - less educational, but fastest
//     simfsVolume->bitvector[0] = 0xC0;
    // 0xC0 is 11000000 in binary (showing the root block and root's index block taken)
    strcpy(simfsContext->bitvector, simfsVolume->bitvector);

    fwrite(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);

    fclose(file);

    return SIMFS_NO_ERROR;
}

/*
 * Takes a folder block and goes through each fileDescriptor pointed to by the index array (the index array pointed to from the folder block_ref)
 * 
 * If another folder is found, pass that folder into the same function to go through that folder.
 *  Otherwise:
 *  	Hash the fileName and store the block_ref at that location in the directory 
 *
 */

SIMFS_ERROR AddFolderToContext(SIMFS_BLOCK_TYPE folder, SIMFS_CONTEXT_TYPE *context){

	if(folder.content.fileDescriptor.size <= 0){
		return SIMFS_NO_ERROR;
	}

	for(int i = 0; i < folder.content.fileDescriptor.size; i++){

		SIMFS_BLOCK_TYPE indexBlock = simfsVolume->block[folder.content.fileDescriptor.block_ref];
		SIMFS_BLOCK_TYPE blockAtIndex = simfsVolume->block[indexBlock.content.index[i]];

		if(blockAtIndex.content.fileDescriptor.type == FOLDER_CONTENT_TYPE){
			AddFolderToContext(blockAtIndex, context);
		}

		context->directory[hash(blockAtIndex.content.fileDescriptor.name)].nodeReference = blockAtIndex.content.fileDescriptor.block_ref;
	}
	return SIMFS_NO_ERROR;
}

/*
 * Loads the file system from a disk and constructs in-memory directory of all files is the system.
 *
 * Starting with the file system root (pointed to from the superblock) traverses the hierarachy of directories
 * and adds en entry for each folder or file to the directory by hashing the name and adding a directory
 * entry node to the conflict resolution list for that entry. If the entry is NULL, the new node will be
 * the only element of that list.
 *
 * The function sets the current working directory to refer to the block holding the root of the volume. This will
 * be changed as the user navigates the file system hierarchy.
 *
 */
SIMFS_ERROR simfsMountFileSystem(char *simfsFileName)
{
    simfsContext = malloc(sizeof(SIMFS_CONTEXT_TYPE));
    if (simfsContext == NULL)
        return SIMFS_ALLOC_ERROR;

    simfsVolume = malloc(sizeof(SIMFS_VOLUME));
    if (simfsVolume == NULL)
        return SIMFS_ALLOC_ERROR;

    FILE *file = fopen(simfsFileName, "rb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    fread(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);

    AddFolderToContext(simfsVolume->block[simfsVolume->superblock.rootNodeIndex], simfsContext);

    strcpy(simfsContext->bitvector, simfsVolume->bitvector);

    fclose(file);
    return SIMFS_NO_ERROR;

    // TODO: complete

}

/*
 * Saves the file system to a disk and de-allocates the memory.
 *
 * Assumes that all synchronization has been done.
 *
 */
SIMFS_ERROR simfsUmountFileSystem(char *simfsFileName)
{
    FILE *file = fopen(simfsFileName, "wb");
    if (file == NULL)
        return SIMFS_ALLOC_ERROR;

    fwrite(simfsVolume, 1, sizeof(SIMFS_VOLUME), file);
    //save the actual files on ur computer...
    fclose(file);

    free(simfsVolume);
    free(simfsContext);

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Depending on the type parameter the function creates a file or a folder in the current directory
 * of the process. If the process does not have an entry in the processControlBlock, then the root directory
 * is assumed to be its current working directory.
 *
 * Hashes the file name and check if the file with such name already exists in the in-memory directory.
 * If it is then it return SIMFS_DUPLICATE_ERROR.
 * Otherwise:
 *    - finds an available block in the storage using the in-memory bitvector and flips the bit to indicate
 *      that the block is taken
 *    - initializes a local buffer for the file descriptor block with the block type depending on the parameter type
 *      (i.e., folder or file)
 *    - creates an entry in the conflict resolution list for the corresponding in-memory directory entry
 *    - copies the local buffer to the disk block that was found to be free
 *    - copies the in-memory bitvector to the bitevector blocks on the simulated disk
 *
 *  The access rights and the the owner are taken from the context (umask and uid correspondingly).
 *
 */
SIMFS_ERROR simfsCreateFile(SIMFS_NAME_TYPE fileName, SIMFS_CONTENT_TYPE type)
{
    // TODO: implement

	unsigned short free = simfsFindFreeBlock(simfsContext->bitvector);
	simfsFlipBit(simfsContext->bitvector, free);

	SIMFS_BLOCK_TYPE curr_block;

    if(simfsContext->processControlBlocks != NULL){
    	curr_block = simfsVolume->block[simfsContext->processControlBlocks->currentWorkingDirectory];
    }
    else{
    	curr_block = simfsVolume->block[simfsVolume->superblock.rootNodeIndex];
    }

    if(curr_block.type != FOLDER_CONTENT_TYPE){
    	printf("Current Directory is not a Folder\n");
    	return SIMFS_NOT_FOUND_ERROR;
    }
    
    SIMFS_BLOCK_TYPE index_block = simfsVolume->block[curr_block.content.fileDescriptor.block_ref];
    if(index_block.type != INDEX_CONTENT_TYPE){
    	printf("CurrentDirectory does not point to Index Block\n");
    	return SIMFS_NOT_FOUND_ERROR;
    }

    SIMFS_NAME_TYPE fileName_actual;

    sprintf(fileName_actual, "%s%s/", curr_block.content.fileDescriptor.name, fileName);

    //printf("OG: %s ACTUAL: %s\n", fileName, fileName_actual);

    //printf("Path: %s\n", fileName_actual);

    SIMFS_DIR_ENT *hash_dir = &(simfsContext->directory[hash(fileName_actual)]);
    //printf("HashedNode%d\n", hash_dir->nodeReference);

    while(hash_dir->nodeReference != 0){
		if(strcmp(simfsVolume->block[hash_dir->nodeReference].content.fileDescriptor.name, fileName_actual) == 0){
			//duplicate found
			return SIMFS_DUPLICATE_ERROR;
		}
		else{
			if(hash_dir->next == NULL){
				hash_dir->next = malloc(sizeof(SIMFS_DIR_ENT));
				hash_dir = hash_dir->next;
				break;
			}
			else{
				hash_dir = hash_dir->next;
			}
		}
	}

	hash_dir->nodeReference = free;
	hash_dir->next = NULL;

	//got here with no errors, so now set up actual file

    SIMFS_FILE_DESCRIPTOR_TYPE fd = simfsVolume->block[free].content.fileDescriptor;

	fd.type = type;
	strcpy(fd.name, fileName_actual);

	fd.block_ref = simfsFindFreeBlock(simfsContext->bitvector);
	simfsFlipBit(simfsContext->bitvector, fd.block_ref);

	SIMFS_CONTENT_TYPE block_ref_type;

	switch(type){
		case FOLDER_CONTENT_TYPE:
			block_ref_type = INDEX_CONTENT_TYPE;
				break;
		case FILE_CONTENT_TYPE:
			block_ref_type = DATA_CONTENT_TYPE;
				break;
		default:
			return SIMFS_ACCESS_ERROR;
	}

	simfsVolume->block[fd.block_ref].type = block_ref_type;

	//DEAL WITH FOLDER SIZE PROBLEMS...
	if(curr_block.content.fileDescriptor.size == SIMFS_INDEX_SIZE - 1){
		//size has reached max, set the final block to point to an index...
		index_block.content.index[curr_block.content.fileDescriptor.size] = simfsFindFreeBlock(simfsContext->bitvector);
		simfsFlipBit(simfsContext->bitvector, index_block.content.index[curr_block.content.fileDescriptor.size]);
		simfsVolume->block[index_block.content.index[SIMFS_INDEX_SIZE]].type = INDEX_CONTENT_TYPE;
	}
	else{
		for(int i = 0; i < curr_block.content.fileDescriptor.size; i++){
			if(index_block.content.index[i] == 0){
				index_block.content.index[i] = free;
				break;
			}
		}
	}

	fd.accessRights = curr_block.content.fileDescriptor.accessRights;
    fd.owner = curr_block.content.fileDescriptor.owner; // arbitrarily simulated
    curr_block.content.fileDescriptor.size++;

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    fd.creationTime = time.tv_sec;
    fd.lastAccessTime = time.tv_sec;
    fd.lastModificationTime = time.tv_sec;
    simfsVolume->block[free].content.fileDescriptor = fd;

    strcpy(simfsVolume->bitvector, simfsContext->bitvector);

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Deletes a file from the file system.
 *
 * Hashes the file name and check if the file is in the directory. If not, then it returns SIMFS_NOT_FOUND_ERROR.
 * Otherwise:
 *    - finds the reference to the file descriptor block
 *    - if the referenced block is a folder that is not empty, then returns SIMFS_NOT_EMPTY_ERROR.
 *    - Otherwise:
 *       - checks if the process owner can delete this file or folder; if not, it returns SIMFS_ACCESS_ERROR.
 *       - Otherwise:
 *          - frees all blocks belonging to the file by flipping the corresponding bits in the in-memory bitvector
 *          - frees the reference block by flipping the corresponding bit in the in-memory bitvector
 *          - clears the entry in the folder by removing the corresponding node in the list associated with
 *            the slot for this file
 *          - copies the in-memory bitvector to the bitvector blocks on the simulated disk
 */
SIMFS_ERROR simfsDeleteFile(SIMFS_NAME_TYPE fileName)
{
    // TODO: implement
    //printf("Deleting: %s\n", fileName);
    SIMFS_DIR_ENT *dir = &(simfsContext->directory[hash(fileName)]);
    if(dir->nodeReference == 0){
    	//nothing hashed there, nothing to delete
    	return SIMFS_NOT_FOUND_ERROR;
    }


    //getting here means the file exists
    SIMFS_BLOCK_TYPE curr_block = simfsVolume->block[dir->nodeReference];

    if(curr_block.content.fileDescriptor.type == FOLDER_CONTENT_TYPE){
    	if(curr_block.content.fileDescriptor.size > 0){
    		return SIMFS_NOT_EMPTY_ERROR;
    	}
    }

    //USE BIT MANIPULATION TO CHECK FOR OWNER
    //U:rwxG:rwxO:rwx
    //022 -> 000 000 001 
    if(curr_block.content.fileDescriptor.accessRights&0001){
    	//if the accessRight's owner execute bit is 1, then the owner can delete files
    	SIMFS_BLOCK_TYPE index_block = simfsVolume->block[curr_block.content.fileDescriptor.block_ref];
    	//free all the blocks in the file
    	for(int i = 0; i < curr_block.content.fileDescriptor.size; i++){
    		simfsClearBit(simfsContext->bitvector, index_block.content.index[i]);
    	}
    	simfsClearBit(simfsContext->bitvector, curr_block.content.fileDescriptor.block_ref);
    	dir->nodeReference = 0;
    	strcpy(simfsVolume->bitvector, simfsContext->bitvector);
    }
    else{
    	return SIMFS_ACCESS_ERROR;
    }

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Finds the file in the in-memory directory and obtains the information about the file from the file descriptor
 * block referenced from the directory.
 *
 * If the file is not found, then it returns SIMFS_NOT_FOUND_ERROR
 */
SIMFS_ERROR simfsGetFileInfo(SIMFS_NAME_TYPE fileName, SIMFS_FILE_DESCRIPTOR_TYPE *infoBuffer)
{
    // TODO: implement
    SIMFS_DIR_ENT *dir = &(simfsContext->directory[hash(fileName)]);

    if(dir->nodeReference == 0)
    	return SIMFS_NOT_FOUND_ERROR;

    SIMFS_FILE_DESCRIPTOR_TYPE fd = simfsVolume->block[dir->nodeReference].content.fileDescriptor;

    infoBuffer->type = fd.type;
    strcpy(infoBuffer->name, fd.name);
    infoBuffer->creationTime = fd.creationTime;

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    fd.lastAccessTime = time.tv_sec;

    infoBuffer->lastAccessTime = fd.lastAccessTime; //UPADTE THIS RIGHT NOW
    infoBuffer->lastModificationTime = fd.lastModificationTime;
    infoBuffer->owner = fd.owner;
    infoBuffer->size = fd.size;
    infoBuffer->block_ref = fd.block_ref;

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Hashes the name and searches for it in the in-memory directory. If the file does not exist,
 * the SIMFS_NOT_FOUND_ERROR is returned.
 *
 * Otherwise:
 *    - checks the per-process open file table for the process, and if the file has already been opened
 *      it returns the index of the openFileTable with the entry of the file through the parameter fileHandle, and
 *      returns SIMFS_DUPLICATE_ERROR as the return value
 *
 *    - otherwise, checks if there is a global entry for the file, and if so, then:
 *       - it increases the reference count for this file
 *
 *       - otherwise, it creates an entry in the global open file table for the file copying the information
 *         from the file descriptor block referenced from the entry for this file in the directory
 *
*       - if the process does not have its process control block in the processControlBlocks list, then
*         a file control block for the process is created and added to the list; the current working directory
*         is initialized to the root of the volume and the number of the open files is initialized to 0
*
*       - if an entry for this file does not exits in the per-process open file table, the function finds an
*         empty slot in the table and fills it with the information including the reference to the entry for
*         this file in the global open file table.
*
*       - returns the index to the new element of the per-process open file table through the parameter fileHandle
*         and SIMFS_NO_ERROR as the return value
 *
 * If there is no free slot for the file in either the global file table or in the per-process
 * file table, or if there is any other allocation problem, then the function returns SIMFS_ALLOC_ERROR.
 *
 */
SIMFS_ERROR simfsOpenFile(SIMFS_NAME_TYPE fileName, SIMFS_FILE_HANDLE_TYPE *fileHandle)
{
    // TODO: implement
    //printf("%s\n", fileName);
    SIMFS_DIR_ENT *dir = &(simfsContext->directory[hash(fileName)]);

    if(dir->nodeReference == 0)
    	return SIMFS_NOT_FOUND_ERROR;

    SIMFS_PROCESS_CONTROL_BLOCK_TYPE *process = simfsContext->processControlBlocks;

    //true/false indicator to keep track of first open block
    unsigned short openBlockFound = 0;
    //variable to hold first open block in per_process table
    int per_pros_open_ind = 0;
    int glob_open_ind = 0;

    while(process != NULL){
    	if(process->numberOfOpenFiles != 0){
    		//has an open file, go through openFile table. keep track of first free open block in per_process
    		for(int i = 0; i < process->numberOfOpenFiles; i++){
    			SIMFS_FILE_DESCRIPTOR_TYPE process_fd = simfsVolume->block[process->openFileTable[i].globalEntry->fileDescriptor].content.fileDescriptor;
    			//check if this fileDescriptor is the same as the fileName passed in...
    			SIMFS_FILE_DESCRIPTOR_TYPE passed_fd = simfsVolume->block[dir->nodeReference].content.fileDescriptor;
    			if(strcmp(process_fd.name, passed_fd.name)==0){
    				//a process' name matches the file passed in, so it is already open
    				*fileHandle = i;
    				return SIMFS_DUPLICATE_ERROR;
    			}
    			else{
    				if(openBlockFound == 0){
    					openBlockFound = 1;
    					per_pros_open_ind = i;
    				}
    			}
    		}//end for loop

    		//check globalTable now. reset the openBlockFound to false
    		openBlockFound = 0;

    		for(int i = 0; i < SIMFS_MAX_NUMBER_OF_OPEN_FILES; i++){
    			if(simfsContext->globalOpenFileTable[i].fileDescriptor > 0){
    				//this file at this index points to an actual file...
    				SIMFS_FILE_DESCRIPTOR_TYPE global_fd =  simfsVolume->block[simfsContext->globalOpenFileTable[i].fileDescriptor].content.fileDescriptor;
    				SIMFS_FILE_DESCRIPTOR_TYPE passed_fd = simfsVolume->block[dir->nodeReference].content.fileDescriptor;

    				//check if this fileDes macthes the one passed in
    				if(strcmp(global_fd.name, passed_fd.name)==0){
    					//found it in the thing...
    					//increase ref count
    					simfsContext->globalOpenFileTable[i].referenceCount++;
    					*fileHandle = i;
    					return SIMFS_NO_ERROR;
    				}
    			}
    			else{
    				//its a free spot, store the free spot and stop looking for free spots.
    				if(openBlockFound == 0){
    					//no open block found...
    					openBlockFound = 1;
    					glob_open_ind = i;
    				}
    			}
    		}//end globalTable for loop
    	} //end if. 
    	else{
    		process = process->next;
    	}
    }//end while loop

    //initialize a processControlBlocks
	process = (SIMFS_PROCESS_CONTROL_BLOCK_TYPE *)malloc(sizeof(SIMFS_PROCESS_CONTROL_BLOCK_TYPE));
	process->currentWorkingDirectory = simfsVolume->superblock.rootNodeIndex;
	process->numberOfOpenFiles = 0;
	process->next = NULL;

	//check if there is room to add a process
    if(per_pros_open_ind > SIMFS_MAX_NUMBER_OF_OPEN_FILES_PER_PROCESS || glob_open_ind > SIMFS_MAX_NUMBER_OF_OPEN_FILES)
    	return SIMFS_ALLOC_ERROR;

    SIMFS_BLOCK_TYPE *openBlock = &simfsVolume->block[dir->nodeReference];

    //per process table accessrights set
    process->openFileTable[per_pros_open_ind].accessRights = openBlock->content.fileDescriptor.accessRights;

    //the context global in-memory, no allocation needed
    simfsContext->globalOpenFileTable[glob_open_ind].type = openBlock->content.fileDescriptor.type;
	simfsContext->globalOpenFileTable[glob_open_ind].fileDescriptor = dir->nodeReference;
	simfsContext->globalOpenFileTable[glob_open_ind].creationTime = openBlock->content.fileDescriptor.creationTime;

	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);

	//update lastAccessTime
	openBlock->content.fileDescriptor.lastAccessTime = time.tv_sec;
	//continue setting values
	simfsContext->globalOpenFileTable[glob_open_ind].lastAccessTime = openBlock->content.fileDescriptor.lastAccessTime;

	simfsContext->globalOpenFileTable[glob_open_ind].lastModificationTime = openBlock->content.fileDescriptor.lastModificationTime;
	simfsContext->globalOpenFileTable[glob_open_ind].owner = openBlock->content.fileDescriptor.owner;
	simfsContext->globalOpenFileTable[glob_open_ind].size = openBlock->content.fileDescriptor.size;


	//process global entry is NOT null, so why issues??
    process->openFileTable[per_pros_open_ind].globalEntry = &simfsContext->globalOpenFileTable[glob_open_ind];
    simfsContext->processControlBlocks = process;

    *fileHandle = per_pros_open_ind;

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * The function replaces content of a file with new one pointed to by the parameter writeBuffer.
 *
 * Checks if the file handle points to a valid file descriptor of an open file. If the entry is invalid
 * (e.g., if the reference to the global table is NULL, or if the entry in the global table is INVALID_CONTENT_TYPE),
 * then it returns SIMFS_NOT_FOUND_ERROR.
 *
 * Otherwise, it checks the access rights for writing. If the process owner is not allowed to write to the file,
 * then the function returns SIMFS_ACCESS_ERROR.
 *
 * Then, the functions calculates the space needed for the new content and checks if the write buffer can fit into
 * the remaining free space in the file system. If not, then the SIMFS_ALLOC_ERROR is returned.
 *
 * Otherwise, the function removes all blocks currently held by this file, and then acquires new blocks as needed
 * modifying bits in the in-memory bitvector as needed.
 *
 * It then copies the characters pointed to by the parameter writeBuffer (until '\0' but excluding it) to the
 * new blocks that belong to the file. The function copies any modified block of the in-memory bitvector to
 * the corresponding bitvector block on the disk.
 *
 * Finally, the file descriptor is modified to reflect the new size of the file, and the times of last modification
 * and access.
 *
 * The function returns SIMFS_WRITE_ERROR in response to exception not specified earlier.
 *
 */
SIMFS_ERROR simfsWriteFile(SIMFS_FILE_HANDLE_TYPE fileHandle, char *writeBuffer)
{
    // TODO: implement
    if(simfsContext->processControlBlocks == NULL)
    	return SIMFS_NOT_FOUND_ERROR;

    if(simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry == NULL)
    	return SIMFS_NOT_FOUND_ERROR;

    if(simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry->type == INVALID_CONTENT_TYPE)
		return SIMFS_NOT_FOUND_ERROR;

	SIMFS_BLOCK_TYPE *write_block = &(simfsVolume->block[simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry->fileDescriptor]);

    if(write_block->content.fileDescriptor.accessRights&0200){
		//user CAN write
		SIMFS_BLOCK_TYPE *data_block = &(simfsVolume->block[write_block->content.fileDescriptor.block_ref]);
		if(data_block->type != DATA_CONTENT_TYPE)
			return SIMFS_NOT_FOUND_ERROR;

		//check if theres room for writeBuffer... max size multiplied by 2 since each file can have up to 2 data blocks
		if((write_block->content.fileDescriptor.size + sizeof(writeBuffer)) > (SIMFS_DATA_SIZE * 2))
			return SIMFS_ALLOC_ERROR;

		//a reference to another data block exists, clear the second data block
		if(data_block->content.data[SIMFS_DATA_SIZE-1] != 0)
			simfsClearBit(simfsContext->bitvector, data_block->content.data[SIMFS_DATA_SIZE]);

		//clear the bit from the original block (thus removing it)
 		simfsClearBit(simfsContext->bitvector, write_block->content.fileDescriptor.block_ref);
 		//and find a new open block
 		write_block->content.fileDescriptor.block_ref = simfsFindFreeBlock(simfsContext->bitvector);

		strcpy(data_block->content.data, writeBuffer);

		//copy in-memory bitvector to volume
		strcpy(simfsVolume->bitvector, simfsContext->bitvector);

		struct timespec time;
		clock_gettime(CLOCK_MONOTONIC, &time);

		//update lastModificationTime
		write_block->content.fileDescriptor.lastModificationTime = time.tv_sec;

		// The function returns SIMFS_WRITE_ERROR in response to exception not specified earlier.
		return SIMFS_WRITE_ERROR;
	}
	else
		return SIMFS_ACCESS_ERROR;

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * The function returns the complete content of the file to the caller through the parameter readBuffer.
 *
 * Checks if the file handle points to a valid file descriptor of an open file. If the entry is invalid
 * (e.g., if the reference to the global table is NULL, or if the entry in the global table is INVALID_CONTENT_TYPE),
 * then it returns SIMFS_NOT_FOUND_ERROR.
 *
 * Otherwise, it checks the user's access right to read the file. If the process owner is not allowed to read the file,
 * then the function returns SIMFS_ACCESS_ERROR.
 *
 * Otherwise, the function allocates memory sufficient to hold the read content with an appended end of string
 * character; the pointer to newly allocated memory is passed back through the readBuffer parameter. All the content
 * of the blocks is concatenated using the allocated space, and an end of string character is appended at the end of
 * the concatenated content.
 *
 * The function returns SIMFS_READ_ERROR in response to exception not specified earlier.
 *
 */
SIMFS_ERROR simfsReadFile(SIMFS_FILE_HANDLE_TYPE fileHandle, char **readBuffer)
{
    // TODO: implement
    if(simfsContext->processControlBlocks == NULL)
    	return SIMFS_NOT_FOUND_ERROR;

    if(simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry == NULL)
    	return SIMFS_NOT_FOUND_ERROR;

    if(simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry->type == INVALID_CONTENT_TYPE)
		return SIMFS_NOT_FOUND_ERROR;

	SIMFS_BLOCK_TYPE read_block = simfsVolume->block[simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry->fileDescriptor];
    if(read_block.content.fileDescriptor.accessRights&0400){

		//user CAN read
		SIMFS_BLOCK_TYPE data_block = simfsVolume->block[read_block.content.fileDescriptor.block_ref];
		if(data_block.type != DATA_CONTENT_TYPE)
			return SIMFS_NOT_FOUND_ERROR;

		//malloc the size of the data + one additional character
		char *read = malloc(read_block.content.fileDescriptor.size + sizeof(char));

		if(read_block.content.fileDescriptor.size == SIMFS_DATA_SIZE)
			return SIMFS_ALLOC_ERROR;

		strcpy(read, data_block.content.data);

		//readBuffer needs to point to a char*
		*readBuffer = read;

		//printf("Message Read IN FUNCTION: %s\n", *readBuffer); //the first element in readBuffer, aka the char *

		// The function returns SIMFS_READ_ERROR in response to exception not specified earlier.
		return SIMFS_READ_ERROR;
	}
	else
		return SIMFS_ACCESS_ERROR;

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////

/*
 * Removes the entry for the file with the file handle provided as the parameter from the open file table
 * for this process. It decreases the number of open files for in the process control block of this process, and
 * if it becomes zero, then the process control block for this process is removed from the processControlBlocks list.
 *
 * Decreases the reference count in the global open file table, and if that number is 0, it also removes the entry
 * for this file from the global open file table.
 *
 */

SIMFS_ERROR simfsCloseFile(SIMFS_FILE_HANDLE_TYPE fileHandle)
{
    // TODO: implement
    simfsContext->processControlBlocks->numberOfOpenFiles--;
    
    if(simfsContext->processControlBlocks->numberOfOpenFiles <= 0){
    	simfsContext->processControlBlocks->numberOfOpenFiles = 0;
    	simfsContext->processControlBlocks = simfsContext->processControlBlocks->next;
    }
    else{
    	simfsContext->processControlBlocks->openFileTable[fileHandle].accessRights = umask(0000);

    	//setting fileDescriptor to 0 allows for it to be overwritten, essentially erasing it.
    	simfsContext->processControlBlocks->openFileTable[fileHandle].globalEntry->fileDescriptor = 0;
    }

    simfsContext->globalOpenFileTable[fileHandle].referenceCount--;

    if(simfsContext->globalOpenFileTable[fileHandle].referenceCount <= 0){
    	simfsContext->globalOpenFileTable[fileHandle].referenceCount = 0;
    	simfsContext->globalOpenFileTable[fileHandle].fileDescriptor = 0;
    }

    return SIMFS_NO_ERROR;
}

//////////////////////////////////////////////////////////////////////////
//
// The following functions are provided only for testing without FUSE.
//
// When linked to the FUSE library, both user ID and process ID can be obtained by calling fuse_get_context().
// See: http://libfuse.github.io/doxygen/structfuse__context.html
//
//////////////////////////////////////////////////////////////////////////

/*
 * Simulates FUSE context to get values for user ID, process ID, and umask through fuse_context
 */

struct fuse_context *simfs_debug_get_context() {

    // TODO: replace its use with FUSE's fuse_get_context()

    struct fuse_context *context = malloc(sizeof(struct fuse_context));

    context->fuse = NULL;
    context->uid = (uid_t) rand()%10+1;
    context->pid = (pid_t) rand()%10+1;
    context->gid = (gid_t) rand()%10+1;
    context->private_data = NULL;
    context->umask = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH; // can be changed as needed

    return context;
}

char *simfsGenerateContent(int size)
{
    size = (size <= 0 ? rand()%1000 : size); // arbitrarily chosen as an example

    char *content = malloc(size);

    int firstPrintable = ' ';
    int len = '~' - firstPrintable;

    for (int i=0; i<size-1; i++)
        *(content+i) = firstPrintable + rand()%len;

    content[size - 1] = '\0';
    return content;
}

SIMFS_ERROR PrintError(SIMFS_ERROR er){
	char *er_msg = "";
	switch(er){
		case SIMFS_NO_ERROR:
			er_msg = "No Error\n";
			break;
    	case SIMFS_ALLOC_ERROR:
    		er_msg = "Alloc Error\n";
    		break;
	    case SIMFS_DUPLICATE_ERROR:
	    	er_msg = "Duplicate Error\n";
	    	break;
	    case SIMFS_NOT_FOUND_ERROR:
	    	er_msg = "Not Found Error\n";
	    	break;
	    case SIMFS_NOT_EMPTY_ERROR:
	    	er_msg = "Not Empty Error\n";
	    	break;
	    case SIMFS_ACCESS_ERROR:
	    	er_msg = "Access Error\n";
	    	break;
	    case SIMFS_WRITE_ERROR:
	    	er_msg = "Write Error\n";
	    	break;
	    case SIMFS_READ_ERROR:
	    	er_msg = "Read Error\n";
	    	break;
	    default:
	    	er_msg = "Invalid Error Type\n";
	    	break;
	}
	printf("%s\n", er_msg);
	return er;
}