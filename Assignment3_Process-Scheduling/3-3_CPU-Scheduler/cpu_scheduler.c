#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAXN 2000
#define MAXT 400000
#define QCAP  100000

typedef struct {
    int pid, arrival, burst;
    int remaining;
    int started;          // 0/1
    double start_chart;   // chart time (no CS)
    double finish_chart;
    int cs_at_start;      // CS count at start
    int cs_at_finish;     // CS count at finish
} Proc;

static Proc P[MAXN];
static int N;

static char gantt[MAXT][12];
static int gantt_len;

typedef struct { int a[QCAP]; int h,t; } Queue;
static void q_init(Queue* q){ q->h=q->t=0; }
static int  q_empty(Queue* q){ return q->h==q->t; }
static void q_push(Queue* q,int v){ q->a[q->t++%QCAP]=v; }
static int  q_pop(Queue* q){ return q->a[q->h++%QCAP]; }

static int cmp_arr(const void* a,const void* b){
    const Proc *x=(const Proc*)a, *y=(const Proc*)b;
    if(x->arrival!=y->arrival) return x->arrival-y->arrival;
    return x->pid-y->pid;
}

static void load_input(const char* path){
    FILE* f=fopen(path,"r");
    if(!f){ fprintf(stderr,"open %s failed\n",path); exit(1); }
    N=0;
    while(N<MAXN){
        int id,at,bt;
        if(fscanf(f,"%d %d %d",&id,&at,&bt)!=3) break;
        P[N].pid=id; P[N].arrival=at; P[N].burst=bt;
        P[N].remaining=bt; P[N].started=0;
        P[N].start_chart=P[N].finish_chart=0.0;
        P[N].cs_at_start=P[N].cs_at_finish=0;
        ++N;
    }
    fclose(f);
    for(int i=0;i<MAXT;i++) gantt[i][0]=0;
    gantt_len=0;
    qsort(P,N,sizeof(Proc),cmp_arr);
}

static void paint(double s,double e,int pid){
    int S=(int)ceil(s-1e-12);
    int E=(int)floor(e-1e-12);
    for(int t=S;t<=E && t<MAXT;t++){
        snprintf(gantt[t], sizeof(gantt[t]), "P%d", pid);
        if(t+1>gantt_len) gantt_len=t+1;
    }
}

static int pick_FCFS(int ready[], int rn){
    if(rn==0) return -1;
    int bi=0;
    for(int i=1;i<rn;i++){
        int ai=P[ready[i]].arrival, ab=P[ready[bi]].arrival;
        if(ai<ab || (ai==ab && P[ready[i]].pid<P[ready[bi]].pid)) bi=i;
    }
    return ready[bi];
}
static int pick_SJF(int ready[], int rn){
    if(rn==0) return -1;
    int bi=0;
    for(int i=1;i<rn;i++){
        int ra=P[ready[i]].burst, rb=P[ready[bi]].burst;
        if(ra<rb ||
           (ra==rb && P[ready[i]].arrival<P[ready[bi]].arrival) ||
           (ra==rb && P[ready[i]].arrival==P[ready[bi]].arrival &&
            P[ready[i]].pid<P[ready[bi]].pid)) bi=i;
    }
    return ready[bi];
}
static int pick_SRTF_idx(int ready[], int rn){
    if(rn==0) return -1;
    int bi=0;
    for(int i=1;i<rn;i++){
        int ra=P[ready[i]].remaining, rb=P[ready[bi]].remaining;
        if(ra<rb ||
           (ra==rb && P[ready[i]].arrival<P[ready[bi]].arrival) ||
           (ra==rb && P[ready[i]].arrival==P[ready[bi]].arrival &&
            P[ready[i]].pid<P[ready[bi]].pid)) bi=i;
    }
    return ready[bi];
}

static void print_stats(double chart_end, int total_cs){
    puts("Gantt Chart:\n");
    for(int i=0;i<gantt_len;i++){
        const char* cell = gantt[i][0] ? gantt[i] : "P?";
        printf("| %s ", cell);
    }
    puts("|");

    double sum_wait=0, sum_tat=0, sum_resp=0;
    for(int i=0;i<N;i++){
        double start_wall  = P[i].start_chart  + P[i].cs_at_start  * 0.1;
        double finish_wall = P[i].finish_chart + P[i].cs_at_finish * 0.1;
        double tat  = finish_wall - P[i].arrival;
        double wait = tat - P[i].burst;
        double resp = start_wall - P[i].arrival;
        sum_wait += wait; sum_tat += tat; sum_resp += resp;
    }
    double avg_w = sum_wait/N;
    double avg_t = sum_tat/N;
    double avg_r = sum_resp/N;

    int total_burst=0; for(int i=0;i<N;i++) total_burst += P[i].burst;
    double total_time = chart_end + total_cs*0.1;
    double util = (total_burst/total_time)*100.0;

    printf("\nAverage Waiting Time = %.2f\n", avg_w);
    printf("Average Turnaround Time = %.2f\n", avg_t);
    printf("Average Response Time = %.2f\n", avg_r);
    printf("CPU Utilization = %.2f%%\n", util);
}

// FCFS/SJF (non-preemptive)
static void simulate_nonpreemptive(int mode_fcfs){
    double t=0.0; int next=0, done=0;
    int total_cs=0, last_pid=-1;
    int ready[MAXN]; int rn=0;

    while(done<N){
        while(next<N && P[next].arrival<=t+1e-12){ ready[rn++]=next; next++; }
        if(rn==0){
            if(next<N){ t=P[next].arrival; continue; }
            break;
        }

        int pick = (mode_fcfs? pick_FCFS(ready,rn) : pick_SJF(ready,rn));
        int pos=-1; for(int i=0;i<rn;i++){ if(ready[i]==pick){ pos=i; break; } }
        if(pos!=-1){
            for(int i=pos;i<rn-1;i++){ ready[i]=ready[i+1]; }
            rn--;
        }

        if(last_pid!=-1 && P[pick].pid!=last_pid) total_cs++;
        last_pid=P[pick].pid;

        if(!P[pick].started){
            P[pick].started=1; P[pick].start_chart=t; P[pick].cs_at_start=total_cs;
        }

        double s=t, e=t+P[pick].remaining;
        while(next<N && P[next].arrival<=e+1e-12){ ready[rn++]=next; next++; }
        paint(s,e,P[pick].pid);
        t=e;
        P[pick].remaining=0;
        P[pick].finish_chart=t;
        P[pick].cs_at_finish=total_cs;
        done++;
    }
    print_stats(t,total_cs);
}

// Round Robin
static void simulate_RR(int tq){
    double t=0.0; int next=0, done=0;
    Queue q; q_init(&q);
    int total_cs=0, last_pid=-1;

    while(done<N){
        while(next<N && P[next].arrival<=t+1e-12){ q_push(&q,next); next++; }
        if(q_empty(&q)){ if(next<N){ t=P[next].arrival; continue; } break; }

        int i=q_pop(&q);
        if(last_pid!=-1 && P[i].pid!=last_pid) total_cs++;
        last_pid=P[i].pid;

        if(!P[i].started){
            P[i].started=1; P[i].start_chart=t; P[i].cs_at_start=total_cs;
        }

        int slice = P[i].remaining < tq ? P[i].remaining : tq;
        for(int ms=0; ms<slice; ++ms){
            paint(t,t+1.0,P[i].pid);
            t+=1.0; P[i].remaining--;
            while(next<N && P[next].arrival<=t+1e-12){ q_push(&q,next); next++; }
            if(P[i].remaining==0) break;
        }
        if(P[i].remaining==0){
            P[i].finish_chart=t; P[i].cs_at_finish=total_cs; done++;
        }else{
            q_push(&q,i);
        }
    }
    print_stats(t,total_cs);
}

// SRTF
static void simulate_SRTF(void){
    double t=0.0; int next=0, done=0;
    int total_cs=0;
    int prev_pid=-1;  // last running pid
    int have_run=0;

    while(done<N){
        int any_ready=0;
        for(int i=0;i<N;i++) if(P[i].remaining>0 && P[i].arrival<=t+1e-12){ any_ready=1; break; }
        if(!any_ready){ if(next<N){ t=P[next].arrival; continue; } break; }

        int ready[MAXN]; int rn=0;
        for(int i=0;i<N;i++) if(P[i].remaining>0 && P[i].arrival<=t+1e-12) ready[rn++]=i;
        int pick = pick_SRTF_idx(ready,rn);

        if(have_run && P[pick].pid!=prev_pid) total_cs++;
        prev_pid=P[pick].pid;
        have_run=1;

        if(!P[pick].started){
            P[pick].started=1; P[pick].start_chart=t; P[pick].cs_at_start=total_cs;
        }

        paint(t,t+1.0,P[pick].pid);
        t+=1.0;
        P[pick].remaining--;
        if(P[pick].remaining==0){
            P[pick].finish_chart=t; P[pick].cs_at_finish=total_cs; done++;
            // keep prev_pid as finished pid so next different PID counts as CS
        }
        while(next<N && P[next].arrival<=t+1e-12) next++;
    }
    print_stats(t,total_cs);
}

static void up(char*s){ for(;*s;++s)*s=(char)toupper((unsigned char)*s); }

int main(int argc,char**argv){
    if(argc<3){
        fprintf(stderr,"Usage: %s input_file [FCFS|SJF|RR|SRTF] [time_quantum]\n",argv[0]);
        return 1;
    }
    load_input(argv[1]);
    char alg[16]; strncpy(alg,argv[2],sizeof(alg)); alg[sizeof(alg)-1]=0; up(alg);

    if(strcmp(alg,"FCFS")==0){
        simulate_nonpreemptive(1);
    }else if(strcmp(alg,"SJF")==0){
        simulate_nonpreemptive(0);
    }else if(strcmp(alg,"RR")==0){
        if(argc<4){ fprintf(stderr,"RR requires time_quantum\n"); return 1; }
        int tq=atoi(argv[3]); if(tq<=0){ fprintf(stderr,"Invalid time_quantum\n"); return 1; }
        simulate_RR(tq);
    }else if(strcmp(alg,"SRTF")==0){
        simulate_SRTF();
    }else{
        fprintf(stderr,"Unknown algorithm\n"); return 1;
    }
    return 0;
}

