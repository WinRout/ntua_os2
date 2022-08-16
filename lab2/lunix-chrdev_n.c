/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * < Your name here >
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;

/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;

	WARN_ON ( !(sensor = state->sensor));

	if (state->buf_timestamp != sensor->msr_data[state->type]->
		last_update) return 1;
	else return 0;
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	int ret;
	uint32_t data;
	unsigned long flags;
	long res;

	sensor = state->sensor;

		/*
		* Grab the raw data quickly, hold the
		* spinlock for as little as possible.
		*/
	spin_lock_irqsave(&sensor->lock, flags);
	state->buf_timestamp = sensor->msr_data[state->type]->last_update;
	data = sensor->msr_data[state->type]->values[0];
	spin_unlock_irqrestore(&sensor->lock, flags);
	/* Why use spinlocks? See LDD3, p. 119 */

	/*
	 * Any new data available?
	 */
	 if (!lunix_chrdev_state_needs_refresh(state)) ret = -EAGAIN;
 	else {
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
	 long value;
	 switch (state->type) {
		 case BATT: value = lookup_voltage[data];
		 case TEMP: value = lookup_temperature[data];
		 case LIGHT: value = lookup_light;
	 }
	 state->buf_lim = snprintf(state->buf_data, LUNIX_CHRDEV_BUFZ, "%d.%d\n",
 	value/1000, value%1000);
	}

	debug("leaving\n");
	return ret;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/

static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
	unsigned int major;
	unsigned int minor;
	unsigned int type;
	unsigned int sensor_no;
	struct lunix_chrdec_state_struct *dev;

	major = imajor(inode); //get the major number
	minor = iminor(inode); //get the minor number
	if (major != 60) goto out; //major number has to be 60
	type = minor & (0x07); //get the type of sensor (2 LSBs)
	sensor_no = (minor - type) >> 3; //division by 8

	int ret;

	debug("entering\n");
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */

	/* Allocate a new Lunix character device private state structure */
	dev = kmalloc(sizeof(struct lunix_chrdev_state_struct), GFP_KERNEL);
	dev->type = type;
	dev->sensor = &lunix_sensors[sensor_no];
	dev->buf_lim = 0;
	dev->buf_timestamp = 0; //why
	sema_init(&dev->lock, 1); //initialize semaphore to 1

	filp->private_data = dev;


out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Why? */
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	ssize_t ret;

	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;

	state = filp->private_data;
	WARN_ON(!state);

	sensor = state->sensor;
	WARN_ON(!sensor);

	/* Lock? */
	if(down_interruptible(&state->lock)) //if there's an interruption, restart
		return -ERESTARTSYS;
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
	if (*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
			up(&state->lock);

	if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

			/* The process needs to sleep */
			/* See LDD3, page 153 for a hint */

			if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state))) {
				return -ERESTRARTSYS;
			}
			if (down_interruptible(&state->lock)){
				return -ERESTARTSYS;
			}
		}
	}

	/* End of file */


	/* Determine the number of cached bytes to copy to userspace */
	int bytes_left = stage->buf_lim - *f_pos;

	ssize_t size = cnt;
	cnt = min(cnt, bytes_left);
	if (size > state->buf_lim)
		size = state->buf_lim;

	if (copy_to_user(usrbuf, state->buf_date, size)) {
		ret = -EFAULT;
		goto out;
	}
	*f_pos += size;
	ret = *f_pos;
	*f_pos = 0;

	/* Auto-rewind on EOF mode? */
	/* ? */
out:
	/* Unlock? */
	up(&state->lock);
	return ret;
}

static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static struct file_operations lunix_chrdev_fops =
{
        .owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_1cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;

	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	/* ? */
	ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");
	/* register_chrdev_region? */
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}
	/* ? */

	/* cdev_add? */
	ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
