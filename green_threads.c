#include <assert.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MAX_THREADS (4)

static const int s_stack_size = 0x400000;

struct green_thread {
	struct gt_context {
		uint64_t rsp;
		uint64_t r15;
		uint64_t r14;
		uint64_t r13;
		uint64_t r12;
		uint64_t rbx;
		uint64_t rbp;
	} context;
	enum {
		Unused,
		Running,
		Ready,
	} state;
};

struct green_thread gt_table[MAX_THREADS];
struct green_thread *current_gt;

void gt_init();
void gt_return(int ret);
void gt_switch(struct gt_context *old, struct gt_context *new);
bool gt_schedule();
static void gt_stop();
int gt_create(void (*function)());
static void gt_alarm_handler(int signum);

void gt_init()
{
	// Set up the signal handler
	
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	
	action.sa_handler = gt_alarm_handler;
	action.sa_flags = SA_NODEFER;
	
	if (sigaction(SIGALRM, &action, NULL) != 0) {
		fputs("Error in sigaction()\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	// Set up the timer
	
	const struct itimerval timer = {
		.it_interval = {
			.tv_sec = 0,
			.tv_usec = 20000
		},
		.it_value = {
			.tv_sec = 0,
			.tv_usec = 20000
		}
	};
	
	if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
		fputs("Error in setitimer()\n", stderr);
		exit(EXIT_FAILURE);
	}
	
	current_gt = &gt_table[0];
	current_gt->state = Running;
}

void __attribute__((noreturn))
gt_return(int exitValue)
{
	if (current_gt != &gt_table[0]) {
		current_gt->state = Unused;
		gt_schedule();
		assert(!"reachable");
	}

	while (gt_schedule()) {
		;
	}

	exit(exitValue);
}

bool gt_schedule()
{
	struct green_thread *next_gt = current_gt;
	while (next_gt->state != Ready) {
		next_gt++;
		if (next_gt == &gt_table[MAX_THREADS]) {
			next_gt = &gt_table[0];
		}
		if (next_gt == current_gt) {
			return false;
		}
	}

	if (current_gt->state != Unused) {
		current_gt->state = Ready;
	}
	next_gt->state = Running;

	struct gt_context *old = &current_gt->context;
	current_gt = next_gt;
	gt_switch(old, &next_gt->context);

	return true;
}

static void gt_stop() {
	gt_return(0);
}

int gt_create(void (*function)())
{
	struct green_thread *p;
	for (p = &gt_table[0];; p++) {
		if (p == &gt_table[MAX_THREADS]) {
			return -1;
		} else if (p->state == Unused) {
			break;
		}
	}

	char *stack = malloc(s_stack_size);
	if (!stack) {
		return -1;
	}

	*(uint64_t *)&stack[s_stack_size -  8] = (uint64_t)gt_stop;
	*(uint64_t *)&stack[s_stack_size - 16] = (uint64_t)function;
	p->context.rsp = (uint64_t)&stack[s_stack_size - 16];
	p->state = Ready;

	return 0;
}

void gt_alarm_handler(int signum)
{
	gt_schedule();
}

void do_work()
{
	static int x;

	int id = ++x;
	for (uint64_t i = 0; i < 10000000; i++) {
		printf("%d %lu\n", id, i);
	}
}

int main()
{
	gt_init();
	gt_create(do_work);
	gt_create(do_work);
	gt_return(1);
}
