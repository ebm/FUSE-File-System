#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

/* You need to change this macro to your TFS mount point*/
#define TESTDIR "/tmp/ebm86/mountdir"
#define FILEPERM 0666
#define DIRPERM 0755

#define AMT_FILES 100
#define TEXT_TO_WRITE "#example_text_to_write_____________________________________________________________________________"
#define TOTAL_BYTES 8000
#define PADDING_TEXT "-"

char buf [2 * TOTAL_BYTES];
void print_buf() {
    printf("buf: ");
    int count = 0;
    for (int i = 0; i < TOTAL_BYTES; i++) {
        if (buf[i] == 0) { 
            break;           
            printf("-");
        }
        else {
            count++;
            printf("%c", buf[i]);
        }
    }
    printf("\nSize: %d.\n", count);
}
void write_message_to_file(int fd, char* input) {
    //printf("Write: (%s) to file %d.\n", input, fd);
    int size;
    if ((size = write(fd, input, strlen(input))) != strlen(input)) {
		printf("File (%d) write size = %d wrong\n", fd - 3, size);
		exit(1);
	}
    //printf("File write success size: %d.\n", size);
}
void read_message_from_file(int fd) {
    memset(buf, 0, TOTAL_BYTES);
    int size;
    //lseek(fd, 4096, SEEK_SET);
    if ((size = read(fd, buf, TOTAL_BYTES * 2)) != TOTAL_BYTES) {
		printf("File read size = %d wrong\n", size);
		exit(1);
	}
    printf("File read success size: %d.\n", size);
    print_buf();
    printf("======================\n");
}
int create_file(char* input) {
    int fd = 0;
	if ((fd = creat(input, FILEPERM)) < 0) {
		perror("creat");
		printf("File create failure\n");
		exit(1);
	}
    close(fd);
    if ((fd = open(input, FILEPERM)) < 0) {
		perror("open");
		exit(1);
	}
    //printf("fd = %d, filename = %s.\n", fd, input);
    return fd;
}
void create_directory(char* input) {
    printf("Create directory: %s.\n", input);
    if (mkdir(input, DIRPERM) < 0) {
		perror("mkdir");
		printf("mkdir failure.\n");
		exit(1);
	}
}
int count_digits(int num) {
    if (num == 0) return 1;
    int res = 0;
    while (num != 0) {
        num /= 10;
        res++;
    }
    return res;
}
void set_names(int index, char* folder_names, char* file_names, char* message_names) {
    char* folder = TESTDIR "/folder";
    char* file = "/file";
    char* text = TEXT_TO_WRITE;
    int adder = count_digits(index);

    for (int i = 0; i < strlen(folder); i++) {
        folder_names[i] = folder[i];
        file_names[i] = folder[i];
    }
    for (int i = strlen(folder) + adder; i < strlen(folder) + strlen(file) + adder; i++) {
        file_names[i] = file[i - (strlen(folder) + adder)];
    }
    for (int i = 0; i < strlen(text); i++) {
        message_names[i] = text[i];
    }
    //printf("index: %d, adder: %d.\n", index, adder);
    for (int i = 0; i < adder; i++) {
        //printf("%d, index: %d.\n", (int) strlen(text) + adder - i, index % 10);
        folder_names[strlen(folder) + adder - i - 1] = index % 10 + '0';
        file_names[strlen(folder) + adder - i - 1] = index % 10 + '0';
        file_names[strlen(file) + strlen(folder) + 2 * adder - i - 1] = index % 10 + '0';
        message_names[strlen(text) + adder - i - 1] = index % 10 + '0';
        index /= 10;
    }
    //printf("%ld.\n", strlen(text) + adder + 1);
    folder_names[strlen(folder) + adder + 1] = '\0';
    file_names[strlen(file) + strlen(folder) + 2 * adder] = '\0';
    message_names[strlen(text) + adder + 1] = '\0';
}
int main(int argc, char **argv) {
    char folder_names[256] = {0};
    char file_names[256] = {0};
    char message_names[256] = {0};
    
    int fd[AMT_FILES];
    for (int i = 0; i < AMT_FILES; i++) {
        set_names(i, folder_names, file_names, message_names);
        printf("%s, %s, %s.\n", folder_names, file_names, message_names);
        create_directory(folder_names);
        fd[i] = create_file(file_names);
        int bytes_written = 0;
        for (int j = 0; j < TOTAL_BYTES / strlen(message_names); j++) {
            write_message_to_file(fd[i], message_names);
            bytes_written += strlen(message_names);
        }
        int bytes_left = TOTAL_BYTES - bytes_written;
        for (int j = 0; j < bytes_left; j++) {
            write_message_to_file(fd[i], PADDING_TEXT);
            bytes_written += strlen(PADDING_TEXT);
        }
        printf("bytes_written: %d, bytes_left: %d.\n", bytes_written, bytes_left);        
        lseek(fd[i], 0, SEEK_SET);
    }
    for (int i = 0; i < AMT_FILES; i++) {
        set_names(i, folder_names, file_names, message_names);
        int bytes_written = 0;
        for (int j = 0; j < TOTAL_BYTES / strlen(message_names); j++) {
            write_message_to_file(fd[i], message_names);
            bytes_written += strlen(message_names);
        }
        int bytes_left = TOTAL_BYTES - bytes_written;
        for (int j = 0; j < bytes_left; j++) {
            write_message_to_file(fd[i], PADDING_TEXT);
            bytes_written += strlen(PADDING_TEXT);
        }
        printf("bytes_written: %d, bytes_left: %d.\n", bytes_written, bytes_left);        
        lseek(fd[i], 0, SEEK_SET);
    }

    for (int i = 0; i < AMT_FILES; i++) {
        printf("Read from file %d.\n", i);
        read_message_from_file(fd[i]);
    }

	return 0;
}
