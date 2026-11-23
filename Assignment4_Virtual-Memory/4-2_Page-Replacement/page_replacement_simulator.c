#include <stdio.h>
#include <stdlib.h>

#define MAX_REF   10000
#define MAX_FRAME 1000

int frames[MAX_FRAME];      // Page frames
int ref_bits[MAX_FRAME];    // For Clock algorithm
int last_used[MAX_FRAME];   // For LRU algorithm

// Return index of page in frames, or -1 if not found
int find_page(int page, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        if (frames[i] == page) return i;
    }
    return -1;
}

// Initialize all frames and helper arrays
void init_frames(int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        frames[i] = -1;
        ref_bits[i] = 0;
        last_used[i] = 0;
    }
}

// Optimal page replacement
int simulate_opt(int refs[], int nref, int frame_count) {
    init_frames(frame_count);
    int faults = 0;

    for (int i = 0; i < nref; i++) {
        int page = refs[i];

        // Hit
        if (find_page(page, frame_count) != -1) {
            continue;
        }

        // Page fault
        faults++;

        // Use empty frame first if exists
        int placed = 0;
        for (int f = 0; f < frame_count; f++) {
            if (frames[f] == -1) {
                frames[f] = page;
                placed = 1;
                break;
            }
        }
        if (placed) continue;

        // Choose victim using future information
        int victim = -1;
        int farthest = -1;

        for (int f = 0; f < frame_count; f++) {
            int next_use = -1;
            for (int k = i + 1; k < nref; k++) {
                if (refs[k] == frames[f]) {
                    next_use = k;
                    break;
                }
            }
            if (next_use == -1) {
                victim = f;
                break;
            }
            if (next_use > farthest) {
                farthest = next_use;
                victim = f;
            }
        }

        if (victim == -1) victim = 0;
        frames[victim] = page;
    }

    return faults;
}

// FIFO page replacement
int simulate_fifo(int refs[], int nref, int frame_count) {
    init_frames(frame_count);
    int faults = 0;
    int pointer = 0;  // Oldest frame index

    for (int i = 0; i < nref; i++) {
        int page = refs[i];

        // Hit
        if (find_page(page, frame_count) != -1) {
            continue;
        }

        // Page fault
        faults++;

        frames[pointer] = page;
        pointer = (pointer + 1) % frame_count;
    }

    return faults;
}

// LRU page replacement
int simulate_lru(int refs[], int nref, int frame_count) {
    init_frames(frame_count);
    int faults = 0;
    int time = 0;  // Logical time

    for (int i = 0; i < nref; i++) {
        int page = refs[i];
        time++;

        int pos = find_page(page, frame_count);
        if (pos != -1) {
            // Hit: update last used time
            last_used[pos] = time;
            continue;
        }

        // Page fault
        faults++;

        // Empty frame first
        int empty = -1;
        for (int f = 0; f < frame_count; f++) {
            if (frames[f] == -1) {
                empty = f;
                break;
            }
        }
        if (empty != -1) {
            frames[empty] = page;
            last_used[empty] = time;
            continue;
        }

        // Choose least recently used frame
        int victim = 0;
        int min_time = last_used[0];
        for (int f = 1; f < frame_count; f++) {
            if (last_used[f] < min_time) {
                min_time = last_used[f];
                victim = f;
            }
        }

        frames[victim] = page;
        last_used[victim] = time;
    }

    return faults;
}

// Clock page replacement
int simulate_clock(int refs[], int nref, int frame_count) {
    init_frames(frame_count);
    int faults = 0;
    int hand = 0;  // Clock hand

    for (int i = 0; i < nref; i++) {
        int page = refs[i];

        int pos = find_page(page, frame_count);
        if (pos != -1) {
            // Hit: give second chance
            ref_bits[pos] = 1;
            continue;
        }

        // Page fault
        faults++;

        // Find victim using clock algorithm
        while (1) {
            // Empty frame: use it directly
            if (frames[hand] == -1) {
                frames[hand] = page;
                ref_bits[hand] = 1;
                hand = (hand + 1) % frame_count;
                break;
            }

            // Reference bit == 0: replace this page
            if (ref_bits[hand] == 0) {
                frames[hand] = page;
                ref_bits[hand] = 1;
                hand = (hand + 1) % frame_count;
                break;
            }

            // Reference bit == 1: clear and move hand
            ref_bits[hand] = 0;
            hand = (hand + 1) % frame_count;
        }
    }

    return faults;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s inputfile\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        printf("Cannot open file: %s\n", argv[1]);
        return 1;
    }

    int frame_count;
    int refs[MAX_REF];
    int nref = 0;

    if (fscanf(fp, "%d", &frame_count) != 1) {
        printf("Invalid input file format\n");
        fclose(fp);
        return 1;
    }

    if (frame_count <= 0 || frame_count > MAX_FRAME) {
        printf("Invalid frame count: %d\n", frame_count);
        fclose(fp);
        return 1;
    }

    while (nref < MAX_REF && fscanf(fp, "%d", &refs[nref]) == 1) {
        nref++;
    }

    fclose(fp);

    if (nref == 0) {
        printf("No page reference string found in input\n");
        return 1;
    }

    int opt_faults  = simulate_opt(refs, nref, frame_count);
    int fifo_faults = simulate_fifo(refs, nref, frame_count);
    int lru_faults  = simulate_lru(refs, nref, frame_count);
    int clk_faults  = simulate_clock(refs, nref, frame_count);

    double opt_rate  = 100.0 * opt_faults  / nref;
    double fifo_rate = 100.0 * fifo_faults / nref;
    double lru_rate  = 100.0 * lru_faults  / nref;
    double clk_rate  = 100.0 * clk_faults  / nref;

    printf("Optimal Algorithm:\n");
    printf("Number of Page Faults: %d\n", opt_faults);
    printf("Page Fault Rate: %.2f%%\n\n", opt_rate);

    printf("FIFO Algorithm:\n");
    printf("Number of Page Faults: %d\n", fifo_faults);
    printf("Page Fault Rate: %.2f%%\n\n", fifo_rate);

    printf("LRU Algorithm:\n");
    printf("Number of Page Faults: %d\n", lru_faults);
    printf("Page Fault Rate: %.2f%%\n\n", lru_rate);

    printf("Clock Algorithm:\n");
    printf("Number of Page Faults: %d\n", clk_faults);
    printf("Page Fault Rate: %.2f%%\n", clk_rate);

    return 0;
}

