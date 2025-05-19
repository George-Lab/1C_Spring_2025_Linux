#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/device.h>

#define DEVICE_NAME "fifo_device"
#define FIFO_BUFFER_SIZE 1024

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Egor Labareshnykh");
MODULE_DESCRIPTION("A FIFO driver for inter-process communication");

static int major_number;
static struct cdev fifo_cdev;
static struct class *fifo_class;

/* FIFO buffer and related variables */
static char fifo_buffer[FIFO_BUFFER_SIZE];
static size_t read_pos = 0;
static size_t write_pos = 0;
static size_t buffer_count = 0;

/* Synchronization */
static DEFINE_MUTEX(fifo_mutex);
static DECLARE_WAIT_QUEUE_HEAD(read_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_queue);

/* Function declarations */
static int fifo_open(struct inode *, struct file *);
static int fifo_release(struct inode *, struct file *);
static ssize_t fifo_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t fifo_write(struct file *, const char __user *, size_t, loff_t *);

/* File operations structure */
static const struct file_operations fifo_fops = {
    .owner = THIS_MODULE,
    .open = fifo_open,
    .release = fifo_release,
    .read = fifo_read,
    .write = fifo_write,
};

static int fifo_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "FIFO: Device opened\n");
    return 0;
}

static int fifo_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "FIFO: Device closed\n");
    return 0;
}

static ssize_t fifo_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    size_t bytes_to_read;
    size_t bytes_read = 0;

    if (mutex_lock_interruptible(&fifo_mutex))
        return -ERESTARTSYS;

    /* Wait until there's data to read */
    while (buffer_count == 0) {
        mutex_unlock(&fifo_mutex);
        
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
            
        if (wait_event_interruptible(read_queue, buffer_count > 0))
            return -ERESTARTSYS;
            
        if (mutex_lock_interruptible(&fifo_mutex))
            return -ERESTARTSYS;
    }

    /* Calculate how many bytes to read */
    bytes_to_read = min(count, buffer_count);

    /* Read data from the buffer */
    while (bytes_read < bytes_to_read) {
        if (put_user(fifo_buffer[read_pos], buf + bytes_read)) {
            mutex_unlock(&fifo_mutex);
            return -EFAULT;
        }
        
        read_pos = (read_pos + 1) % FIFO_BUFFER_SIZE;
        bytes_read++;
        buffer_count--;
    }

    mutex_unlock(&fifo_mutex);
    
    /* Wake up processes waiting to write */
    wake_up_interruptible(&write_queue);
    
    return bytes_read;
}

static ssize_t fifo_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    size_t bytes_to_write;
    size_t bytes_written = 0;

    if (mutex_lock_interruptible(&fifo_mutex))
        return -ERESTARTSYS;

    /* Calculate available space */
    bytes_to_write = min(count, FIFO_BUFFER_SIZE - buffer_count);

    /* If no space available, wait */
    while (buffer_count == FIFO_BUFFER_SIZE) {
        mutex_unlock(&fifo_mutex);
        
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
            
        if (wait_event_interruptible(write_queue, buffer_count < FIFO_BUFFER_SIZE))
            return -ERESTARTSYS;
            
        if (mutex_lock_interruptible(&fifo_mutex))
            return -ERESTARTSYS;
            
        bytes_to_write = min(count, FIFO_BUFFER_SIZE - buffer_count);
    }

    /* Write data to the buffer */
    while (bytes_written < bytes_to_write) {
        if (get_user(fifo_buffer[write_pos], buf + bytes_written)) {
            mutex_unlock(&fifo_mutex);
            return -EFAULT;
        }
        
        write_pos = (write_pos + 1) % FIFO_BUFFER_SIZE;
        bytes_written++;
        buffer_count++;
    }

    mutex_unlock(&fifo_mutex);
    
    /* Wake up processes waiting to read */
    wake_up_interruptible(&read_queue);
    
    return bytes_written;
}

static int __init fifo_init(void)
{
    dev_t dev;
    int err;

    /* Allocate a major number dynamically */
    err = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (err < 0) {
        printk(KERN_ERR "FIFO: Failed to allocate major number\n");
        return err;
    }
    major_number = MAJOR(dev);

    /* Initialize the character device */
    cdev_init(&fifo_cdev, &fifo_fops);
    fifo_cdev.owner = THIS_MODULE;

    /* Add the character device to the system */
    err = cdev_add(&fifo_cdev, dev, 1);
    if (err < 0) {
        printk(KERN_ERR "FIFO: Failed to add device to the system\n");
        unregister_chrdev_region(dev, 1);
        return err;
    }

    /* Create the device class - updated for newer kernel API */
    fifo_class = class_create(DEVICE_NAME);
    if (IS_ERR(fifo_class)) {
        printk(KERN_ERR "FIFO: Failed to create device class\n");
        cdev_del(&fifo_cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(fifo_class);
    }

    /* Create the device file */
    if (IS_ERR(device_create(fifo_class, NULL, dev, NULL, DEVICE_NAME))) {
        printk(KERN_ERR "FIFO: Failed to create device\n");
        class_destroy(fifo_class);
        cdev_del(&fifo_cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(fifo_class);
    }

    printk(KERN_INFO "FIFO: Device registered with major number %d\n", major_number);
    printk(KERN_INFO "FIFO: Device created at /dev/%s\n", DEVICE_NAME);
    
    return 0;
}

static void __exit fifo_exit(void)
{
    dev_t dev = MKDEV(major_number, 0);
    
    /* Remove the device */
    device_destroy(fifo_class, dev);
    class_destroy(fifo_class);
    
    /* Remove the character device */
    cdev_del(&fifo_cdev);
    unregister_chrdev_region(dev, 1);
    
    printk(KERN_INFO "FIFO: Device unregistered\n");
}

module_init(fifo_init);
module_exit(fifo_exit);