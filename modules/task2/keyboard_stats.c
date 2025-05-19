#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/interrupt.h>

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Egor Labareshnykh");
MODULE_DESCRIPTION("Key press counter");

// Counter
static atomic_t press_count = ATOMIC_INIT(0);  // Atomic counter for key presses

// IRQ handler for PS/2 keyboard (IRQ 1)
static irqreturn_t irq_handler(int irq, void* dev_id) {
    atomic_inc(&press_count);  // Increment the counter on each key press
    return IRQ_NONE;
}

// Timer structure and callback functions
static struct timer_list timer;

// Function to reschedule the timer for the next interval (60 seconds)
static void reschedule_timer(void) {
    mod_timer(&timer, jiffies + msecs_to_jiffies(60000));  // Reschedule for 1 minute (60000 ms)
}

// Timer callback function that prints the key press statistics
static void callback(struct timer_list* _) {
    int number = atomic_read(&press_count);  // Read the current count of key presses
    pr_info("Characters typed in the last minute: %d\n", number);  // Log the count to kernel log
    atomic_set(&press_count, 0);  // Reset the counter after logging
    reschedule_timer();  // Reschedule the timer for the next minute
}

// Module Initialization
static int __init counter_init(void) {
    int err;

    pr_info("Key press counter module loaded\n");

    // Setup the timer and start it immediately
    timer_setup(&timer, callback, 0);
    reschedule_timer();

    // Request IRQ for PS/2 keyboard (IRQ 1)
    err = request_irq(1,  // PS/2 keyboard IRQ
                      irq_handler,
                      IRQF_SHARED,  // Allow shared IRQs
                      "Key press counter",
                      (void*) irq_handler);
    if (err) {
        del_timer(&timer);  // Clean up the timer if IRQ request fails
        pr_err("request_irq failed with error code %d\n", err);  // Log the error
        return err;
    }

    return 0;
}

// Module Exit (Cleanup)
static void __exit counter_exit(void) {
    pr_info("Key press counter module unloaded\n");
    free_irq(1, (void*) irq_handler);  // Free the IRQ when unloading the module
    del_timer(&timer);  // Delete the timer to prevent further callbacks
}

module_init(counter_init);
module_exit(counter_exit);
