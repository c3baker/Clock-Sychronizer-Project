/*
*
*  Adapted by Sam Siewert for use with UVC web cameras and Bt878 frame
*  grabber NTSC cameras to acquire digital video from a source,
*  time-stamp each frame acquired, save to a PGM or PPM file.
*
*  The original code adapted was open source from V4L2 API and had the
*  following use and incorporation policy:
*
*  This program can be used and distributed without restrictions.
*
*      This program is provided with the V4L2 API
* see http://linuxtv.org/docs.php for more information
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <time.h>

struct v4l2_mm_frame_buffer
{
        void   *start;
        size_t  length;
};

enum io_method
{
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
};

void v4l2_process_image(const void *p, int size);
int v4l2_read_frame(struct v4l2_mm_frame_buffer** frame_buffer);
int v4l2_stop_capturing(void);
int v4l2_start_capturing(void);
int v4l2_uninit_device(void);
int v4l2_init_device(enum io_method init_io_method);
void v4l2_close_device(void);
void v4l2_open_device(void);
int v4l2_requeue_frame_capture_buffer(struct v4l2_mm_frame_buffer* frame_buffer);

