/*
 *  linux/init/main.c
 *
 * 初始化程序 init/main.c
 * 该程序首先确定如何分配使用系统物理内存，然后调用内核各部分的初始化函数分别对
 * 内存管理、中断处理、块设备和字符设备、进程管理以及硬盘和软盘硬件进行初始化处理。
 * 在完成了这些操作之后，系统各部分已经处于可运行状态。
 * 此后程序把自己“手工”移动到任务 0（进程 0）中运行，并使用 fork()调用首次创建出进程 1。
 * 在进程 1 中程序将继续进行应用环境的初始化并执行 shell 登录程序。
 * 而原进程 0 则会在系统空闲时被调度执行，此时任务 0 仅执行 pause()系统调用，并又会调用调度函数。
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 * 我们需要这个内联——从内核空间分叉将导致写入时没有副本，直到执行 execve。
 * 这没有问题，但是对于栈来说。 这是通过在 fork() 之后完全不让 main() 使用堆栈来处理的。
 * 因此，没有函数调用——这也意味着 fork 的内联代码，否则我们将在退出“fork()”时使用堆栈。
 * 实际上内联只需要 pause 和 fork，这样 main() 中的堆栈就不会出现任何混乱，但我们也定义了一些其他的。
 */
static inline _syscall0(int, fork) static inline _syscall0(int, pause) static inline _syscall1(int, setup, void *, BIOS) static inline _syscall0(int, sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

	static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm *tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 * 这是在启动时由设置例程设置的
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 *
 * 是的，是的，它很难看，但我找不到如何正确地做到这一点，这似乎有效。
 * 我有任何人对我感兴趣的实时时钟有更多信息。
 * 其中大部分是反复试验，以及一些 bios 列表阅读。
 * 呃。。。
 */

#define CMOS_READ(addr) ({     \
	outb_p(0x80 | addr, 0x70); \
	inb_p(0x71);               \
})

#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val) >> 4) * 10)

static void time_init(void)
{
	struct tm time;

	do
	{
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info
{
	char dummy[32];
} drive_info;

void main(void) /* This really IS void, no error here. 这真的是无效的，这里没有错误 */
{				/* The startup routine assumes (well, ...) this 启动例程假设 */
				/*
				 * Interrupts are still disabled. Do necessary setups, then enable them
				 * 中断仍然被禁用。 进行必要的设置，然后启用它们
				 */
	ROOT_DEV = ORIG_ROOT_DEV;
	drive_info = DRIVE_INFO;
	memory_end = (1 << 20) + (EXT_MEM_K << 10);
	memory_end &= 0xfffff000;
	if (memory_end > 16 * 1024 * 1024)
		memory_end = 16 * 1024 * 1024;
	if (memory_end > 12 * 1024 * 1024)
		buffer_memory_end = 4 * 1024 * 1024;
	else if (memory_end > 6 * 1024 * 1024)
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);
#endif
	mem_init(main_memory_start, memory_end);
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();
	move_to_user_mode(); // 移动到任务 0 中执行
						 // 把 main.c 程序执行流从内核态（特权级 0）移动到了用户态（特权级 3）的任务 0 中继续运行
	if (!fork())
	{ /* we count on this going ok */ // Linux 系统中创建新进程使用 fork()系统调用。所有进程都是通过复制进程 0 而得到的，都是进程 0 的子进程
		init();
	}
	/*
	 *   NOTE!!   For any other task 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0 'pause()' just means we go check if some other
	 * task can run, and if not we return here.
	 * NOTE!! 对于任何其他任务，'pause()' 意味着我们必须获得唤醒信号，但任务 0 是唯一的例外（请参阅 'schedule()' ）
	 * 因为任务 0 在每个空闲时刻（没有其他任务可以运行时）都会被激活
	 * 对于 task0 'pause()' 只是意味着我们去检查一些其他任务是否可以运行，如果不是我们返回这里
	 */
	for (;;)
		pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char *argv_rc[] = {"/bin/sh", NULL};
static char *envp_rc[] = {"HOME=/", NULL};

static char *argv[] = {"-/bin/sh", NULL};
static char *envp[] = {"HOME=/usr/root", NULL};

void init(void)
{
	int pid, i;

	setup((void *)&drive_info);
	(void)open("/dev/tty0", O_RDWR, 0);
	(void)dup(0);
	(void)dup(0);
	printf("%d buffers = %d bytes buffer space\n\r", NR_BUFFERS,
		   NR_BUFFERS * BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r", memory_end - main_memory_start);
	if (!(pid = fork()))
	{
		close(0);
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);
		execve("/bin/sh", argv_rc, envp_rc);
		_exit(2);
	}
	if (pid > 0)
		while (pid != wait(&i))
			/* 
			 * 在子进程在执行期间，父进程通常使用 wait()或 waitpid()函数等待其某个子进程终止
			 * 当等待的子进程被终止并处于僵死状态时，父进程就会把子进程运行所使用的时间累加到自己进程中
			 * 最终释放已终止子进程任务数据结构所占用的内存页面，并置空子进程在任务数组中占用的指针项
			 */
			/* nothing */;
	while (1)
	{
		if ((pid = fork()) < 0)
		{
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid)
		{
			close(0);
			close(1);
			close(2);
			setsid();
			(void)open("/dev/tty0", O_RDWR, 0);
			(void)dup(0);
			(void)dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync();
	}
	_exit(0); /* NOTE! _exit, not exit() */
}
