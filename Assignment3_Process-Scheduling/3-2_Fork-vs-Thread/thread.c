#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#ifndef MAX_PROCESSES
#define MAX_PROCESSES 8
#endif

typedef struct {
    int idx;  // 0..N-1
    int *out;  // partial 결과 배열
} ThreadArg;

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

static void *worker(void *arg_) {
    ThreadArg *arg = (ThreadArg *)arg_;
    int i = arg->idx;
    int a = read_int_at_line("temp.txt", 2*i + 1);
    int b = read_int_at_line("temp.txt", 2*i + 2);
    arg->out[i] = (a >= 0 && b >= 0) ? (a + b) : 0;  // MAX=64에서 255 이하
    return NULL;
}

int main(void) {
    const int N = MAX_PROCESSES;
    int *partial = (int *)calloc(N, sizeof(int));
    if (!partial) { perror("calloc"); return 1; }
    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t)*N);
    ThreadArg *args = (ThreadArg *)malloc(sizeof(ThreadArg)*N);
    if (!th || !args) { perror("malloc"); free(partial); free(th); free(args); return 1; }

    // temp.txt 존재 확인
    {
        FILE *chk = fopen("temp.txt", "r");
        if (!chk) { fprintf(stderr, "temp.txt not found. Run ./numgen first.\n");
            free(partial); free(th); free(args); return 2; }
        fclose(chk);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    // 1) Leaf 스레드 생성
    for (int i = 0; i < N; ++i) {
        args[i].idx = i;
        args[i].out = partial;
        if (pthread_create(&th[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create");
            for (int k = 0; k < i; ++k) pthread_join(th[k], NULL);
            free(partial); free(th); free(args);
            return 3;
        }
    }
    for (int i = 0; i < N; ++i) pthread_join(th[i], NULL);

    // 캡처용: leaf 값 출력
    print_array("leaf: ", partial, N);

    // 2) 트리형 결합 (메인 스레드가 상위 depth 1개 이용)
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

    printf("[thread/tree] MAX_PROCESSES=%d\n", MAX_PROCESSES);
    printf("Final total = %d\n", final_total);
    printf("Elapsed: %.3f ms\n", ms);

    free(args); free(th); free(partial);
    return 0;
}

