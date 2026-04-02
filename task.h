//task.h
#pragma once
#include "types.h"
#include "memory.h"
extern uint32_t* g_fb;
extern uint32_t g_width;
void draw_string(uint32_t* fb, uint32_t width, const char* str, uint32_t x, uint32_t y, uint32_t color);
#define TASK_STACK_SIZE 65536   // 64KB na stos każdego procesu
#define MAX_TASKS       16

enum TaskState {
    TASK_RUNNING,
    TASK_READY,
    TASK_DEAD
};

struct Task {
    uint64_t rsp;        // zapisany stack pointer — MUSI BYĆ PIERWSZY
    uint64_t id;
    TaskState state;
    Task* next;
    uint8_t* stack;      // wskaźnik na zaalokowany stos
};

// Rejestr który context switch zapisuje na stosie
struct CpuState {
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;  // to pushuje CPU przy przerwaniu
};

extern "C" Task* current_task;
extern "C" Task* task_list;
extern "C" uint64_t next_id;
Task* task_create(void (*entry)())
{
  //  draw_string(g_fb, g_width, "tc1", 200, 110, 0xFFFF00);
    Task* task = (Task*)malloc(sizeof(Task));
    if (!task) return nullptr;
   // draw_string(g_fb, g_width, "tc2", 210, 110, 0xFFFF00);
    // Alokujemy trochę więcej miejsca, żeby bezpiecznie wyrównać
    uint8_t* stack_raw = (uint8_t*)malloc(TASK_STACK_SIZE + 32);
    if (!stack_raw) {
        free(task);
        return nullptr;
    }
   // draw_string(g_fb, g_width, "tc3", 220, 110, 0xFFFF00);

    // Wyrównujemy szczyt stosu do 16 bajtów (standardowy wymóg System V ABI na x86_64)
    uint64_t stack_top_addr = (uint64_t)stack_raw + TASK_STACK_SIZE;
    stack_top_addr &= ~0xFULL;           // wyrównanie w dół do 16 B

    uint64_t* sp = (uint64_t*)stack_top_addr;
   // draw_string(g_fb, g_width, "tc4", 230, 110, 0xFFFF00);
    task->id = next_id++;
    task->state = TASK_READY;
    task->stack = stack_raw;             // zapamiętujemy surowy wskaźnik do free()
    task->next = nullptr;
  //  draw_string(g_fb, g_width, "tc5", 240, 110, 0xFFFF00);
    // ───────────────────────────────────────────────
    //   IREQT frame (5 × 8 bajtów) – będzie zdejmowane przez iretq
    // ───────────────────────────────────────────────
    *--sp = 0x10;                        // SS
    *--sp = stack_top_addr;              // RSP (oryginalny wyrównany szczyt stosu)
    *--sp = 0x202;                       // RFLAGS (IF=1, IOPL=0, reszta domyślna)
    *--sp = 0x08;                        // CS  (kernel code segment)
    *--sp = (uint64_t)entry;             // RIP
   // draw_string(g_fb, g_width, "tc6", 250, 110, 0xFFFF00);
    // ───────────────────────────────────────────────
    //   Rejestry przywracane przez context_switch
    //   Kolejność musi być dokładnie odwrotna do zapisywania w przełączniku!
    // ───────────────────────────────────────────────
    *--sp = 0;     // RAX
    *--sp = 0;     // RBX
    *--sp = 0;     // RCX
    *--sp = 0;     // RDX
    *--sp = 0;     // RSI
    *--sp = 0;     // RDI
    *--sp = 0;     // RBP
    *--sp = 0;     // R8
    *--sp = 0;     // R9
    *--sp = 0;     // R10
    *--sp = 0;     // R11
    *--sp = 0;     // R12
    *--sp = 0;     // R13
    *--sp = 0;     // R14
    *--sp = 0;     // R15

    task->rsp = (uint64_t)sp;

   // draw_string(g_fb, g_width, "tc6a", 200, 120, 0xFFFF00);
    if (task_list == nullptr) {
       // draw_string(g_fb, g_width, "tc6b null", 200, 130, 0xFFFF00);
        task_list = task;
       // draw_string(g_fb, g_width, "tc6c", 200, 140, 0xFFFF00);
        task->next = task;
       // draw_string(g_fb, g_width, "tc6d", 200, 150, 0xFFFF00);
        current_task = task;
       // draw_string(g_fb, g_width, "tc6e", 200, 160, 0xFFFF00);
    }
    else {
        Task* t = task_list;
        while (t->next != task_list) {
            t = t->next;
        }
        t->next = task;
        task->next = task_list;
    }
   // draw_string(g_fb, g_width, "tc7", 260, 110, 0xFFFF00);
    return task;
}