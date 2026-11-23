#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_NUM 100       // max number of files
#define NUM_BLOCKS 1024        // total number of blocks
#define BLOCK_SIZE 32          // size of one block (bytes)
#define MAX_FILE_NAME 100      // max length of file name
#define FS_STAT "fs_state.dat" // file for persisting file-system state

// One file entry in the directory
typedef struct {
    char filename[MAX_FILE_NAME]; // file name
    int start_block;              // index of first block in data_area
    int size;                     // file size in bytes (<= BLOCK_SIZE)
} FileEnt;

// In-memory simple FAT-based file system
typedef struct {
    int fat_table[NUM_BLOCKS];              // 0 = free, 0xFFFF = end of chain
    FileEnt directory[MAX_FILE_NUM];        // directory entries
    char data_area[NUM_BLOCKS * BLOCK_SIZE];// raw data storage area
} FileSystem;

// Global file-system instance
FileSystem myfat;

int create_file(const char *filename);
int write_file(const char *filename, const char *data);
int read_file(const char *filename);
int delete_file(const char *name);
void list_files(void);

void save_file_system(void);
void load_file_system(void);

// Save current file-system image to disk
void save_file_system(void) {
    FILE *f = fopen(FS_STAT, "wb");
    if (f == NULL) {
        printf("Error: Could not save file system state.\n");
        return;
    }
    fwrite(&myfat, sizeof(FileSystem), 1, f);
    fclose(f);
}

// Load file-system image from disk (if it exists)
void load_file_system(void) {
    FILE *f = fopen(FS_STAT, "rb");
    if (f == NULL) {
        printf("Warning: No saved state found. Starting fresh.\n");
        return;
    }
    fread(&myfat, sizeof(FileSystem), 1, f);
    fclose(f);
}

// Create a new empty file and allocate one free block for it
int create_file(const char *filename) {
    // check duplicate file name
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (strcmp(myfat.directory[i].filename, filename) == 0) {
            printf("File '%s' already exists.\n", filename);
            return -1;
        }
    }

    // find empty directory entry
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (myfat.directory[i].filename[0] == '\0') {
            // find free block in FAT
            for (int j = 0; j < NUM_BLOCKS; j++) {
                if (myfat.fat_table[j] == 0) {
                    myfat.fat_table[j] = 0xFFFF;  // mark as end of chain

                    strcpy(myfat.directory[i].filename, filename);
                    myfat.directory[i].start_block = j;
                    myfat.directory[i].size = 0;
                    printf("File '%s' created.\n", filename);
                    return 0;
                }
            }
        }
    }

    printf("File system full. Cannot create more files.\n");
    return -1;
}

// Append data to an existing file (single-block, size-limited)
int write_file(const char *filename, const char *data) {
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (strcmp(myfat.directory[i].filename, filename) == 0) {
            int start_block = myfat.directory[i].start_block;
            int block = start_block;
            int remaining_data = strlen(data);
            int data_offset = 0;  // not used, kept for readability

            if (start_block == -1) {
                printf("File '%s' not initialized. Use create command first.\n", filename);
                return -1;
            }

            // find current end position within this block
            int block_end = BLOCK_SIZE;
            while (block_end > 0 &&
                   myfat.data_area[block * BLOCK_SIZE + block_end - 1] == 0) {
                block_end--;
            }

            // check if new data fits into remaining space of the block
            if (remaining_data <= BLOCK_SIZE - block_end) {
                strncpy(&myfat.data_area[block * BLOCK_SIZE + block_end],
                        &data[data_offset],
                        remaining_data);
                myfat.directory[i].size += remaining_data;

                printf("Data written to '%s'.\n", filename);
                return 0;
            } else {
                // reject if file would exceed single-block capacity
                printf("Data is too Big!!!!\n");
                return -1;
            }
        }
    }

    printf("File '%s' not found.\n", filename);
    return -1;
}

// Read and print the content of a file (up to its recorded size)
int read_file(const char *filename) {
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (strcmp(myfat.directory[i].filename, filename) == 0) {
            int start_block = myfat.directory[i].start_block;
            int block = start_block;
            int total_size = myfat.directory[i].size;
            int bytes_read = 0;  // for extension to multi-block

            if (start_block == -1) {
                printf("File '%s' not initialized. Use create command first.\n",
                       filename);
                return -1;
            }

            printf("Content of '%s': ", filename);

            int remaining_bytes_in_block = total_size - bytes_read;
            int read_size = (remaining_bytes_in_block < BLOCK_SIZE)
                                ? remaining_bytes_in_block
                                : BLOCK_SIZE;
            printf("%.*s", read_size,
                   &myfat.data_area[block * BLOCK_SIZE]);

            printf("\n");
            return 0;
        }
    }

    printf("File '%s' not found.\n", filename);
    return -1;
}

// Delete a file: free its block and clear directory entry
int delete_file(const char *filename) {
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (strcmp(myfat.directory[i].filename, filename) == 0) {
            int start_block = myfat.directory[i].start_block;
            int block = start_block;

            // follow FAT chain and free all blocks (here usually only one)
            while (block != 0xFFFF) {
                int next_block = myfat.fat_table[block];
                myfat.fat_table[block] = 0;
                block = next_block;
            }

            myfat.directory[i].filename[0] = '\0';
            printf("File '%s' deleted.\n", filename);

            return 0;
        }
    }

    printf("File '%s' not found.\n", filename);
    return -1;
}

// Print all files and their sizes
void list_files(void) {
    printf("Files in the file system:\n");
    for (int i = 0; i < MAX_FILE_NUM; i++) {
        if (myfat.directory[i].filename[0] != '\0') {
            printf("File: %s, Size: %d bytes\n",
                   myfat.directory[i].filename,
                   myfat.directory[i].size);
        }
    }
}

// Dispatch a single command from argv
void execute_cmd(char *cmd, char *filename, char *data, int num) {
    if (strcmp(cmd, "create") == 0) {
        if (num != 3)
            printf("Usage: create <filename>\n");
        else
            create_file(filename);
    } else if (strcmp(cmd, "write") == 0) {
        if (num != 4)
            printf("Usage: write <filename> <data>\n");
        else
            write_file(filename, data);
    } else if (strcmp(cmd, "read") == 0) {
        if (num != 3)
            printf("Usage: read <filename>\n");
        else
            read_file(filename);
    } else if (strcmp(cmd, "delete") == 0) {
        if (num != 3)
            printf("Usage: delete <filename>\n");
        else
            delete_file(filename);
    } else if (strcmp(cmd, "list") == 0) {
        list_files();
    } else {
        printf("Invalid command.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        printf("USAGE : ./fat <COMMAND> [ARGS]...\n");
        exit(1);
    }

    load_file_system();  // restore previous state if exists
    execute_cmd(argv[1], argv[2], argv[3], argc);
    save_file_system();  // persist updated state
    exit(0);
}

