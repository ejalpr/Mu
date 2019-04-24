#ifndef _H_EMU
#define _H_EMU

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "cpu.h"
#include "mem.h"

#include "../emulator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Can also be set manually
#if !defined(__i386__) && !defined(__x86_64__) && !(defined(__arm__) && !defined(__thumb__)) && !(defined(__aarch64__))
#define NO_TRANSLATION
#endif

// on iOS, setjmp and longjmp are broken
#ifdef IS_IOS_BUILD
#define NO_SETJMP
#endif

// Helper for micro-optimization
#define unlikely(x) __builtin_expect(x, 0)
#define likely(x) __builtin_expect(x, 1)
static inline uint16_t BSWAP16(uint16_t x) { return x << 8 | x >> 8; }
#define BSWAP32(x) __builtin_bswap32(x)

extern int cycle_count_delta __asm__("cycle_count_delta");
extern int throttle_delay;
extern uint32_t cpu_events __asm__("cpu_events");
#define EVENT_IRQ 1
#define EVENT_FIQ 2
#define EVENT_RESET 4
#define EVENT_DEBUG_STEP 8
#define EVENT_WAITING 16

// Settings
extern bool exiting, debug_on_start, debug_on_warn, print_on_warn;
extern bool do_translate;

enum { LOG_CPU, LOG_IO, LOG_FLASH, LOG_INTS, LOG_ICOUNT, LOG_USB, LOG_GDB, MAX_LOG };
#define LOG_TYPE_TBL "CIFQ#UG"
extern int log_enabled[MAX_LOG];
void logprintf(int type, const char *str, ...);
void emuprintf(const char *format, ...);

//void warn(const char *fmt, ...);
#define warn(...) debugLog(__VA_ARGS__)
//__attribute__((noreturn)) void error(const char *fmt, ...);
static inline __attribute__((noreturn)) void error(const char *fmt, ...){while(true){};}
void throttle_timer_on();
void throttle_timer_off();
void throttle_timer_wait();
int exec_hack();
void add_reset_proc(void (*proc)(void));

// Is actually a jmp_buf, but __builtin_*jmp is used instead
// as the MinGW variant is buggy
extern void *restart_after_exception[32];

// GUI callbacks
/*
void gui_do_stuff(bool wait); // Called every once in a while...
int gui_getchar(); // Serial input
void gui_putchar(char c); // Serial output
*/
#define gui_debug_printf(...) debugLog(__VA_ARGS__)
/*
void gui_debug_vprintf(const char *fmt, va_list ap); // Debug output #2
void gui_perror(const char *msg); // Error output
void gui_set_busy(bool busy); // To change the cursor, for instance
void gui_status_printf(const char *fmt, ...); // Status output
void gui_show_speed(double speed); // Speed display output
void gui_usblink_changed(bool state); // Notification for usblink state changes
void gui_debugger_entered_or_left(bool entered); // Notification for debug events
*/

/* callback == 0: Stop requesting input
 * callback != 0: Call callback with input, then stop requesting */
/*
typedef void (*debug_input_cb)(const char *input);
void gui_debugger_request_input(debug_input_cb callback);

#define SNAPSHOT_SIG 0xCAFEBEE0
#define SNAPSHOT_VER 1

typedef struct emu_snapshot {
    uint32_t sig; // SNAPSHOT_SIG
    uint32_t version; // SNAPSHOT_VER
    int product, asic_user_flags;
    sched_state sched;
    arm_state cpu_state;
    mem_snapshot mem;
    flash_snapshot flash;
} emu_snapshot;
*/

#ifdef __cplusplus
}
#endif

#endif