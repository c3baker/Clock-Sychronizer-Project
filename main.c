#include "libv4l2.h"
#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <mqueue.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sched.h>

#define STD_ERR -1
#define CAPTURE_TASK_PERIOD_MSECS 50
#define SCHEDULER_PERIOD_NSECS 1000000 // 1 milisecond period

typedef enum boolean_valus {
	FALSE = 0,
	TRUE,
}BOOL;

sem_t capture_sem;
BOOL stop_camera;
timer_t sequencer_timer;

int startup_camera_for_capturing(void);
void shutdown_camera(void);
long get_time_in_us(struct timeval* time);
int setup_thread_attributes(pthread_attr_t* thread_attr, int sched_policy, int thread_prio);

long get_time_in_us(struct timeval* time)
{
	return (time->tv_sec * 1000000) + (time->tv_usec);
}


void* capture_task_function(void* capture_task_data)
{
	struct timeval time_stamp;
	int ret = 0;

	while (stop_camera == FALSE)
	{
		sem_wait(&capture_sem);
		gettimeofday(&time_stamp, NULL);
		syslog(LOG_USER, "Read Frame at time: %lu us\n", get_time_in_us(&time_stamp));
		if (0 != (ret = v4l2_read_frame()))
		{
			syslog(LOG_ERR, "Error in read_frame(): %d\n", ret);
		}
		gettimeofday(&time_stamp, NULL);
		syslog(LOG_USER, "Finished reading frame at time: %lu us\n", get_time_in_us(&time_stamp));
	}

	pthread_exit(NULL);
}

int setup_thread_attributes(pthread_attr_t* thread_attr, int sched_policy, int thread_prio)
{
	struct sched_param sched_param;
	cpu_set_t cpu_set;
	int ret = 0;

	CPU_ZERO(&cpu_set);

	if (thread_attr == NULL)
		return STD_ERR;

	CPU_SET(USE_CPU, &cpu_set);

	if (0 != (ret = pthread_attr_init(thread_attr)))
	{
		return ret;
	}

	// Set scheduling policy to SCHED_FIFO
	if (0 != (ret = pthread_attr_setschedpolicy(thread_attr, sched_policy)))
	{
		return ret;
	}

	// Notify the pthread that it should take its scheduling policy from the attribute object
	if (0 != (ret = pthread_attr_setinheritsched(thread_attr, PTHREAD_EXPLICIT_SCHED)))
	{
		return ret;
	}

	sched_param.__sched_priority = thread_prio;

	if (0 != (ret = pthread_attr_setschedparam(thread_attr, &sched_param)))
	{
		return ret;
	}

	if (0 != (ret = pthread_attr_setaffinity_np(thread_attr, sizeof(cpu_set_t), &cpu_set)))
	{
		return ret;
	}

	return ret;
}

int create_tasks()
{
	int ret = 0;
	pthread_attr_t thread_attr;
	void* thread_ret = NULL;
	int capture_task_prio = 0;

	if (0 != (ret = sem_init(&capture_sem, 0, 0)))
		return ret;

	capture_task_prio = sched_get_priority_max(SCHED_FIFO) - 1;
	if (0 != (ret = setup_thread_attributes(&thread_attr, SCHED_FIFO, capture_task_prio)))
	{
		return ret;
	}

	if (0 != (ret = pthread_create(&capture_task, &thread_attr, capture_task_function, NULL)))
	{
		return ret;
	}

	return ret;
}

void kill_tasks()
{
	void* thread_ret = NULL;
	stop_camera = TRUE;
	sem_post(&capture_sem);
	pthread_join(capture_task, (void**)&thread_ret);
}


void task_scheduler(int signo)
{
	static long milisecs = 0;
	if (signo != SIGALRM)
		return;

	if (0 == (milisecs % CAPTURE_TASK_PERIOD_MSECS))
		sem_post(&capture_sem);

	if (0 == (milisecs % STORAGE_TASK_PERIOD_MSECS))
		sem_post(&storage_sem);

	milisecs++;

}


int startup_camera_for_capturing()
{
	v4l2_open_device();
	if (0 != (ret = v4l2_init_device(IO_METHOD_MMAP)))
	{
		v4l2_close_device();
		syslog(LOG_ERR, "Error: Failed to init device: %d\n", ret);
		return ret;
	}
	if (0 != (ret = v4l2_start_capturing()))
	{
		v4l2_close_device();
		syslog(LOG_ERR, "Error: Failed to start capturing: %d\n", ret);
		return ret;
	}

	stop_camera = FALSE;
}

void shutdown_camera()
{
	v4l2_stop_capturing();
	v4l2_uninit_device();
	v4l2_close_device();
}

int create_sequencer_timer(timer_t* timer)
{
	struct sigevent timer_events;

	struct sigaction alarm_action;
	timer_events.sigev_notify = SIGEV_SIGNAL; // Send a signal when the timer expires
	timer_events.sigev_signo = SIGALRM; // Send a SIGALMR signal

	// Setup the timer signal handler (scheduler)
	alarm_action.sa_handler = task_scheduler;
	alarm_action.sa_flags = 0;

	ret = sigaction(SIGALRM, &alarm_action, NULL);
	if (ret != 0)
		return ret;

	ret = timer_create(CLOCK_MONOTONIC, &timer_events, timer);

	return ret;
}

int set_timer(timer_t* timer, long interval_ns, long start_time_ns)
{
	struct itimerspec timer_timespec;

	timer_timespec.it_interval.tv_nsec = interval_ns;
	timer_timespec.it_interval.tv_sec = 0;

	timer_timespec.it_value.tv_nsec = start_time_ns * 1000; //Start after 1 second
	timer_timespec.it_value.tv_sec = 0;
	ret = timer_settime(timer, 0, &timer_timespec, NULL);
	return ret;

}


int main(int argc, char** argv)
{
	int ret = 0;

	openlog("PROJECT_TEST >>>", 0, LOG_USER);

	if (0 != (ret = startup_camera_for_capturing()))
	{
		syslog(LOG_ERR, "Fatal Error -- Starting camera failed. Exiting.\n");
		closelog();
		return STD_ERR;
	}
	
	if (0 != (ret = create_tasks()))
	{
		syslog(LOG_ERR, "Fatal Error -- Failed to start services: %d\n", ret);	
		goto exit;
	}

	if (0 != (ret = create_sequencer_timer(&sequencer_timer))
	{
		syslog(LOG_ERR, "Fatal Error -- Failed to setup sequencer timer: %d\n", ret);
		goto exit_kill_tasks;
	}

	if (0 != (ret = set_timer(&sequencer_timer, SCHEDULER_PERIOD_NSECS, SCHEDULER_PERIOD_NSECS * 1000)))
	{
		syslog(LOG_ERR, "Fatal Error - Failed to activate sequencer timer: %d\n", ret);
		goto exit_kill_tasks;
	}

	while (usleep(1000000) == EINTR); 

	if (0 != (ret = set_timer(&sequencer_timer, 0, 0))) // Deactivate timer
	{
		syslog(LOG_ERR, "Error - Failed to deactivate sequencer timer: %d\n", ret);
		goto exit_kill_tasks;
	}


exit_kill_tasks:
	kill_tasks();
exit:
	shutdown_camera();
	closelog();
	return ret;
}