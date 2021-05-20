#include "libv4l2.h"
#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>

long get_time_in_us(struct timeval* time)
{
	return (time->tv_sec * 1000000) + (time->tv_usec);
}

int main(int argc, char** argv)
{
	int ret = 0;
	int i = 0;
	long time_diff_us = 0;
	struct timeval start_time, finish_time; 
	int num_frames = 0;

	v4l2_open_device();

	openlog("PROJECT_TEST >>>", 0, LOG_USER);
	void syslog(int priority, const char *format, ...);
	void closelog(void);
	if (0 != (ret = v4l2_init_device()))
	{
		v4l2_close_device();
		syslog(LOG_ERR, "Error: Failed to init device: %d\n", ret);
		return -1;
	}
	if (0 != (ret = v4l2_start_capturing()))
	{
		v4l2_close_device();
		syslog(LOG_ERR, "Error: Failed to start capturing: %d\n", ret);
		return -1;
	}
	usleep(10000);
	for (i = 0; i < 10; i++) // Just a warm - up
	{
		if (0 != (ret = v4l2_read_frame()))
		{ 
			usleep(10000);
			syslog(LOG_ERR, "Warm-up: Error reading frame: %d\n", ret);
		}
	}

	if (0 != (ret = gettimeofday(&start_time, NULL)))
	{
		syslog(LOG_ERR, "Failed to get time: %d\n", ret);
	}
	usleep(10000);
	while(num_frames < 100) // The real test
	{
		struct timeval time_stamp_1, time_stamp_2;
		if (0 != (ret = gettimeofday(&time_stamp_1, NULL)))
		{
			syslog(LOG_ERR, "Failed to get time: %d\n", ret);
		}
		if (0 != (ret = v4l2_read_frame()))
		{
			usleep(20000);
			//syslog(LOG_ERR, "Error reading frame: %d\n", ret);
		}
		else
		{
		    num_frames++;
	    	if (0 != (ret = gettimeofday(&time_stamp_2, NULL)))
			{
				syslog(LOG_ERR, "Failed to get time: %d\n", ret);
			}
			else
			{
				syslog(LOG_INFO, "Time taken to read frame in us: %lu \n", get_time_in_us(&time_stamp_2) - get_time_in_us(&time_stamp_1));
			}
		}
	}

	if (0 != (ret = gettimeofday(&finish_time, NULL)))
	{
		syslog(LOG_ERR, "Failed to get time: %d\n", ret);
		goto exit;
	}

	time_diff_us = get_time_in_us(&finish_time) - get_time_in_us(&start_time);
	syslog(LOG_INFO, "Reading %d frames took %lu usecs.\n", num_frames, time_diff_us);

exit:
	v4l2_stop_capturing();
	v4l2_uninit_device();
	v4l2_close_device();

	return 0;
}