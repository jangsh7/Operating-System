#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#ifndef MAX_PROCESSES
#define MAX_PROCESSES 8  // make MAX_PROCESSES=64 로 재빌드 가능
#endif

static double diff_ms(struct timespec a, struct timespec b) {
    long sec  = b.tv_sec  - a.tv_sec;
    long nsec = b.tv_nsec - a.tv_nsec;
    return sec * 1000.0 + nsec / 1e6;
}

static void print_array(const char* tag, const int* a, int n){
    printf("%s", tag);
    for(int i=0;i<n;i++) printf("%s%d", (i? " ":""), a[i]);
    printf("\n");
}

// 1-based line 번호의 정수를 읽기 (실패 시 -1)
static int read_int_at_line(const char *path, int one_based_line) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "fopen(%s): %s\n", path, strerror(errno));
        return -1;
    }
    char buf[128];
    int cur = 0, val = -1;
    while (fgets(buf, sizeof(buf), fp)) {
        ++cur;
        if (cur == one_based_line) {
            if (sscanf(buf, "%d", &val) != 1) val = -1;
            break;
        }
    }
    fclose(fp);
    return val;
}

int main(void) {
    const int N = MAX_PROCESSES;  // 최상단 작업 단위 수
    int *partial = (int *)calloc(N, sizeof(int));
    if (!partial) { perror("calloc"); return 1; }
    pid_t *pids = (pid_t *)malloc(sizeof(pid_t)*N);
    if (!pids) { perror("malloc"); free(partial); return 1; }

    // temp.txt 존재 확인
    {
        FILE *chk = fopen("temp.txt", "r");
        if (!chk) {
            fprintf(stderr, "temp.txt not found. Run ./numgen first.\n");
            free(partial); free(pids);
            return 2;
        }
        fclose(chk);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // 1) Leaf: N개의 자식 생성, 각자 2줄 읽어 합산 후 exit(8bit)
    for (int i = 0; i < N; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at i=%d: %s\n", i, strerror(errno));
            for (int k = 0; k < i; ++k) wait(NULL);
            free(partial); free(pids);
            return 3;
        }
        if (pid == 0) {
            int l1 = 2 * i + 1;
            int l2 = 2 * i + 2;
            int a = read_int_at_line("temp.txt", l1);
            int b = read_int_at_line("temp.txt", l2);
            int sum = (a >= 0 && b >= 0) ? (a + b) : 0;  // MAX=64에서 최대 255
            _exit(sum & 0xFF);  // 8비트만 전달
        } else {
            pids[i] = pid;
        }
    }

    // 2) 부모: 자식 결과 수집
    for (int i = 0; i < N; ++i) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            free(partial); free(pids);
            return 4;
        }
        partial[i] = (WIFEXITED(status) ? WEXITSTATUS(status) : 0);
    }
    free(pids);

    // 캡처용: leaf 값 출력
    print_array("leaf: ", partial, N);

    // 3) 트리형 결합 (상위 depth 1개 이용)
    int len = N, level = 1;
    while (len > 1) {
        int next = 0;
        for (int i = 0; i + 1 < len; i += 2)
            partial[next++] = partial[i] + partial[i + 1];
        if (len % 2 == 1) partial[next++] = partial[len - 1];
        char tag[64];
        snprintf(tag, sizeof(tag), "level(%d): ", level++);
        print_array(tag, partial, next);
        len = next;
    }

    int final_total = partial[0];
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = diff_ms(t0, t1);

    printf("[fork/tree] MAX_PROCESSES=%d\n", MAX_PROCESSES);
    printf("Final total = %d\n", final_total);
    printf("Elapsed: %.3f ms\n", ms);
    printf("Note: leaf children returned 8-bit via exit(); parent combined by tree reduction.\n");

    free(partial);
    return 0;
}

