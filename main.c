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
#include <assert.h>
#include <mqueue.h>
#include <sys/stat.h>


#define STD_ERR -1
#define CAPTURE_TASK_PERIOD_MSECS 50
#define SCHEDULER_PERIOD_NSECS 1000000 // 1 millisecond period
#define USE_CPU 2
#define MAX_QUEUE_MSGS 30

typedef enum boolean_valus {
	FALSE = 0,
	TRUE,
}BOOL;

sem_t capture_sem;
BOOL stop_camera = TRUE;
timer_t sequencer_timer;
pthread_t capture_task;
mqd_t captured_image_queue = 0;

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
	struct v4l2_mm_frame_buffer* frame_buffer = NULL;
	while (stop_camera == FALSE)
	{
		sem_wait(&capture_sem);
		gettimeofday(&time_stamp, NULL);
		syslog(LOG_USER, "Read Frame at time: %lu us\n", get_time_in_us(&time_stamp));
		if (0 != (ret = v4l2_read_frame(&frame_buffer)))
		{
			syslog(LOG_ERR, "Error in read_frame(): %d\n", ret);
		}

		if(frame_buffer != NULL)
		{
		    if(0 != (ret = mq_send(captured_image_queue, (char*)frame_buffer, sizeof(struct v4l2_mm_frame_buffer), 0)))
		    {
		        syslog(LOG_ERR, "Error in message queue for image capture: %d\n", ret);
		    }
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

	assert(thread_attr != NULL);

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

	milisecs++;
}


int startup_camera_for_capturing()
{
    int ret = 0;

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
	return ret;
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
	int ret = 0;
	struct sigaction alarm_action;

    assert(timer != NULL);
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
    int ret = 0;

    assert(timer != NULL);
	timer_timespec.it_interval.tv_nsec = interval_ns;
	timer_timespec.it_interval.tv_sec = 0;

	timer_timespec.it_value.tv_nsec = start_time_ns * 1000; //Start after 1 second
	timer_timespec.it_value.tv_sec = 0;
	ret = timer_settime(timer, 0, &timer_timespec, NULL);

	return ret;
}

int create_task_message_queues()
{
    int ret = 0;
    int msg_queue_flags = (O_RDWR | O_CREAT | O_NONBLOCK);
    struct mq_attr msg_queue_attrs;

    msg_queue_attrs.mq_maxmsg = MAX_QUEUE_MSGS;
    msg_queue_attrs.mq_msgsize = sizeof(struct v4l2_mm_frame_buffer); // Messages will be pointers
    captured_image_queue = mq_open("image-queue",msg_queue_flags, S_IRWXU, &msg_queue_attrs);

    if(captured_image_queue < 0)
    {
        syslog(LOG_ERR, "Error: Failed to open message queue: %d\n", errno);
        ret = errno;
    }

    return ret;
}

void close_task_message_queues()
{
    if(captured_image_queue > 0)
    {
        if(0 != mq_close(captured_image_queue))
        {
            syslog(LOG_ERR, "Error: Closing Captured Image Queue: %d\n", errno);
            // We are exiting anyway if this function is being called
            // So just print the error to the syslog no need to return anything
        }
    }
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
	
	if(0 != (ret = create_task_message_queues()))
	{
	    syslog(LOG_ERR, "Fatal Error -- Failed to open message queues: %d\n", ret);
	    goto exit;
	}

	if (0 != (ret = create_tasks()))
	{
		syslog(LOG_ERR, "Fatal Error -- Failed to start services: %d\n", ret);	
		goto exit_close_queues;
	}

	if (0 != (ret = create_sequencer_timer(&sequencer_timer)))
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
exit_close_queues:
    close_task_message_queues();
exit:
	shutdown_camera();
	closelog();
	return ret;
}
