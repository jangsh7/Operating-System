# 🐧 Operating System

운영체제 과목에서 진행한 과제를 모아둔 저장소입니다.  
Linux Kernel 5.4를 직접 빌드하고, 시스템 콜·커널 모듈·스케줄링·메모리·파일 시스템을 코드 수준에서 구현했습니다.

<br>

## 🗂️ Assignments

|  #  | 과제                | 핵심 내용                                 |                                                           바로가기                                                           |
| :-: | :------------------ | :---------------------------------------- | :--------------------------------------------------------------------------------------------------------------------------: |
|  1  | 커널 빌드 & 수정    | Kernel 컴파일, ctags·cscope, printk       |    [![A1](https://img.shields.io/badge/Assignment_1-Kernel_Build-555555?style=for-the-badge)](./Assignment1_Kernel-Build)    |
|  2  | 시스템 콜 & 후킹    | 시스템 콜 추가, Hooking, 파일 I/O 추적    | [![A2](https://img.shields.io/badge/Assignment_2-System_Call-4A90E2?style=for-the-badge)](./Assignment2_System-Call-Hooking) |
|  3  | 프로세스 & 스케줄링 | task_struct, fork vs thread, CPU 스케줄러 |  [![A3](https://img.shields.io/badge/Assignment_3-Scheduling-7B68EE?style=for-the-badge)](./Assignment3_Process-Scheduling)  |
|  4  | 가상 메모리         | 메모리 영역 분석, 페이지 교체 알고리즘    |  [![A4](https://img.shields.io/badge/Assignment_4-Virtual_Memory-2ECC71?style=for-the-badge)](./Assignment4_Virtual-Memory)  |
|  5  | 파일 시스템         | procfs, FAT 가상 파일 시스템              |     [![A5](https://img.shields.io/badge/Assignment_5-File_System-E74C3C?style=for-the-badge)](./Assignment5_File-System)     |

<br>

## 📝 Assignment Summary

### 1️⃣ 커널 빌드 & 수정

- **개발 환경** : VMware 가상 머신에 Ubuntu 20.04.6 설치
- **커널 빌드** : Linux Kernel 5.4.282 소스를 `menuconfig`로 설정 → 컴파일 → 모듈 설치 → GRUB 등록 → 재부팅
- **코드 탐색** : `ctags`, `cscope`로 커널 함수와 심볼 위치 탐색
- **커널 수정** : `init/main.c`의 `start_kernel()`과 `drivers/char/agp/backend.c`에 `printk`를 추가해 부팅 로그에 학번 출력

### 2️⃣ 시스템 콜 & 후킹

- **2-1 · 시스템 콜 추가** : `SYSCALL_DEFINE1`로 `os_ftrace` 시스템 콜을 만들고 `syscall_64.tbl`에 등록한 뒤 커널 재빌드
- **2-2 · 시스템 콜 후킹** : `/proc/kallsyms`로 `sys_call_table` 주소를 찾고, 커널 모듈에서 테이블 항목을 바꿔 기존 시스템 콜을 새 함수로 대체
- **2-3 · 파일 I/O 추적** : `openat`, `read`, `write`, `lseek`, `close`를 후킹해 지정한 프로세스의 파일 접근 횟수와 읽고 쓴 바이트 수를 기록하는 Trace 모듈 구현

### 3️⃣ 프로세스 & 스케줄링

- **3-1 · Process Tracer** : `task_struct`에 `fork_count` 필드를 추가하고, 커널 모듈로 프로세스 상태, 부모·자식·형제 관계, Context Switch 횟수, fork 횟수 출력
- **3-2 · fork vs pthread** : 같은 계산 작업을 `fork()` 기반 멀티 프로세스와 `pthread` 기반 멀티 스레드로 각각 처리해 수행 시간과 자원 공유 방식 비교
- **3-3 · CPU 스케줄러** : **FCFS, SJF, SRTF, RR** 시뮬레이터를 구현하고 Gantt Chart, 평균 대기 시간, 응답 시간, CPU 활용률로 비교

### 4️⃣ 가상 메모리

- **4-1 · 메모리 영역 분석** : 시스템 콜을 후킹하는 커널 모듈에서 `mm_struct`와 `vm_area_struct`를 따라가며 프로세스의 코드·데이터·힙 영역 주소와 매핑된 파일 경로 출력
- **4-2 · 페이지 교체** : **Optimal, FIFO, LRU, Clock** 알고리즘을 구현하고, 같은 참조 문자열에서 Page Fault 횟수와 비율 비교

### 5️⃣ 파일 시스템

- **5-1 · procfs** : 커널 모듈로 `/proc` 파일을 만들고, 읽을 때 프로세스의 PID, PPID, UID, GID, utime, stime, State를 출력
- **5-2 · FAT 파일 시스템** : 32바이트 블록 1024개와 FAT 테이블로 구성된 가상 파일 시스템 구현
  - 파일 생성·쓰기·읽기·삭제·목록 조회 지원
  - 종료 시 전체 상태를 `fs_state.dat`에 저장하고, 다음 실행 시 복원

<br>

## 🛠️ Tech Stack

![C](https://img.shields.io/badge/C-A8B9CC?style=flat-square&logo=c&logoColor=black)
![Linux Kernel](https://img.shields.io/badge/Linux_Kernel_5.4-FCC624?style=flat-square&logo=linux&logoColor=black)
![Ubuntu](https://img.shields.io/badge/Ubuntu_20.04-E95420?style=flat-square&logo=ubuntu&logoColor=white)
![VMware](https://img.shields.io/badge/VMware-607078?style=flat-square&logo=vmware&logoColor=white)
![GCC](https://img.shields.io/badge/GCC_&_Make-555555?style=flat-square&logo=gnu&logoColor=white)

<br>

## 🚀 How to Run

커널 모듈 과제(2-2, 2-3, 3-1, 4-1, 5-1)는 과제용으로 수정한 커널(5.4.282) 위에서 동작합니다.

```bash
make                     # 모듈 빌드
sudo insmod 모듈이름.ko    # 모듈 적재
dmesg | tail             # 커널 로그 확인
sudo rmmod 모듈이름        # 모듈 해제
```

사용자 영역 프로그램(3-2, 3-3, 4-2, 5-2)은 각 폴더에서 `make` 후 바로 실행할 수 있습니다.

<br>

## 📁 Repository Structure

```
Operating-System/
├── Assignment1_Kernel-Build/
├── Assignment2_System-Call-Hooking/
│   ├── 2-1_New-System-Call/
│   ├── 2-2_Syscall-Hooking/
│   └── 2-3_File-IO-Tracing/
├── Assignment3_Process-Scheduling/
│   ├── 3-1_Process-Tracer/
│   ├── 3-2_Fork-vs-Thread/
│   └── 3-3_CPU-Scheduler/
├── Assignment4_Virtual-Memory/
│   ├── 4-1_Memory-Area-Viewer/
│   └── 4-2_Page-Replacement/
└── Assignment5_File-System/
    ├── 5-1_Proc-Info/
    └── 5-2_FAT-File-System/
```

<br>

## ⚠️ Note

Assignment 1의 `main.c`, `backend.c`는 Linux Kernel 5.4.282 원본 소스(GPL-2.0)에 `printk` 출력 코드만 추가한 파일입니다.  
나머지 코드는 직접 작성했습니다.

<br>

## ©️ Copyright

Copyright © 2025 장서현. All rights reserved.

이 저장소의 코드와 보고서는 학습 및 포트폴리오 목적으로 공개합니다.  
**무단 복제 및 과제 제출 용도로의 사용을 금지합니다.**  
단, Linux Kernel에서 가져온 파일은 원래의 GPL-2.0 라이선스를 따릅니다.
