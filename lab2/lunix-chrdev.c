/*
 * notes.c
 *
 * Notes for lunix-chrdev.c
 *
 * Nikolaos Pagonas
 * Nikitas Tsinnas
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
 * Συναρτήσεις:
 * 
 * lunix_chrdev_state_needs_refresh --> done
 * lunix_chrdev_state_update --> done
 * lunix_chrdev_open --> done
 * lunix_chrdev_release --> done
 * lunix_chrdev_ioctl --> done
 * lunix_chrdev_read --> done
 * lunix_chrdev_mmap --> done
 * lunix_chrdev_init --> done
 * lunix_chrdev_destroy --> done
 */

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

	WARN_ON(!(sensor = state->sensor));
	
	/* ? --> done */
	if(state->buf_timestamp != sensor->msr_data[state->type]->last_update)
		return 1;

	return 0;  
}



/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;

	WARN_ON(!(sensor = state->sensor));
	
	debug("leaving\n"); 

	int type = state->type;
	uint32_t data;
	uint32_t timestamp; 

	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */

	/* ? --> done */
	spin_lock(&sensor->lock);

	data = sensor->msr_data[type]->values[0];
	timestamp = sensor->msr_data[type]->last_update;

	spin_unlock(&sensor->lock);

	/* Why use spinlocks? See LDD3, p. 119 */

	/*
	 * Any new data available?
	 */
	
	/* ? --> done */
	if(state->buf_timestamp == timestamp)
		return -EAGAIN;
	
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */

	/* ? --> done */
	long lookup_value;	

	switch(type) {
		case BATT:
			lookup_value = lookup_voltage[data];
			break;
		case TEMP:
			lookup_value = lookup_temperature[data];
			break;
		case LIGHT:
			lookup_value = lookup_light[data];
			break;
	}

	int integer_part = lookup_value / 1000;
	int decimal_part = lookup_value > 0 ? lookup_value % 1000 : -lookup_value % 1000;

	state->buf_lim = snprintf(state->buf_data, LUNIX_CHRDEV_BUFSZ, "%d.%d\n", integer_part, decimal_part); 
	state->buf_timestamp = timestamp;

	debug("leaving\n");
	return 0;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/


static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
	/* ? --> done? */

	int ret;

	debug("entering\n");

	ret = -ENODEV;

	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */

	unsigned int minor = iminor(inode);

	/* Allocate a new Lunix character device private state structure */
	
	/* ? -> done */
	struct lunix_chrdev_state_struct* state;
	state = (struct lunix_chrdev_state_struct*) kmalloc(sizeof(struct lunix_chrdev_state_struct), GFP_KERNEL);
	
	/* Fill all fields of lunix_chrdev_state_struct */
	state->type = minor & 7; /* ...xxx & ...111, τα 3 LSB */ 
	state->sensor = &lunix_sensors[minor >> 3]; /* xxx... -> xxx, "ξεχνάμε" τα 3 LSB */ 
	state->buf_lim = 0; 
	state->buf_timestamp = 0; 
	sema_init(&state->lock, 1);

	filp->private_data = state;
out:
	debug("leaving, with ret = %d\n", ret);
	return ret; 
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp) 
{
	/* ? --> done */
	kfree(filp->private_data);
	return 0;
}

/*
 * Στην περίπτωσή μας ουσιαστικά δεν την ορίζουμε.
 */ 
static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* 
	 * Why? --> Γιατί ουσιαστικά δεν υλοποιούμε την ioctl, οπότε σύμφωνα με τον οδηγό
	 * πρέπει να επιστρέφουμε -EINVAL.
	 */

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

	/* Lock? --> done */
	if (down_interruptible(&state->lock))
		return -ERESTARTSYS;
	
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
	if (*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
			/* ? --> done */
			/* The process needs to sleep */
			/* See LDD3, page 153 for a hint */
			up(&state->lock);

			if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state))) 
				return -ERESTARTSYS;

			if (down_interruptible(&state->lock)) 
				return -ERESTARTSYS;
		}
	}

	/* End of file */
	/* ? --> done */
	
	/* Determine the number of cached bytes to copy to userspace */
	/* ? --> done */
	int bytes_left = state->buf_lim - *f_pos;

	cnt = min(cnt, bytes_left);

	if (copy_to_user(usrbuf, state->buf_data, cnt)) {
		ret = -EFAULT;
		goto out;
	}

	*f_pos += cnt;
	ret = cnt;

	/* Auto-rewind on EOF mode? */
	/* ? --> done */
	if (*f_pos == state->buf_lim)
		*f_pos = 0;

out:
	/* Unlock? */
	up(&state->lock);
	return ret;
}

/*
 * Στην περίπτωσή μας ουσιαστικά δεν την ορίζουμε.
 */
static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/* 
	 * Πάλι ουσιαστικά δεν υλοποιούμε την mmap,
	 * οπότε επιστρέφουμε -EINVAL.
	 */
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

	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;
	
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0); 
	/* ? --> done */
	/* register_chrdev_region? --> done */
	ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}	
	/* ? --> done */
	/* cdev_add? --> done */	
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
