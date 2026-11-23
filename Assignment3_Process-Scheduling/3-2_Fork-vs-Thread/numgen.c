#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

#ifndef MAX_PROCESSES
#define MAX_PROCESSES 8
#endif

int main(void) {
    const int N = MAX_PROCESSES * 2;
    FILE *fp = fopen("temp.txt", "w");  // file open
    if (!fp) {  // error handling
        perror("fopen");
        return 1;
    }
    for (int i = 1; i <= N; ++i) {
        fprintf(fp, "%d\n", i);
    }
    fclose(fp);  // file close
    printf("Generated temp.txt with %d lines (1..%d)\n", N, N);
    return 0;
}

