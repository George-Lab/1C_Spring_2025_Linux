#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/pgtable.h>

#define PROCFS_NAME "mmaneg"
#define BUFFER_SIZE 256

static struct proc_dir_entry *our_proc_file;

// Function to list all VMAs of the current process
static void list_vmas(void)
{
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    
    mm = get_task_mm(current);
    if (!mm) {
        printk(KERN_ERR "mmaneg: Failed to get mm_struct for current process\n");
        return;
    }
    
    printk(KERN_INFO "mmaneg: VMAs for process %d (%s):\n", current->pid, current->comm);
    
    down_read(&mm->mmap_lock);
    
    // Use find_vma to iterate through all VMAs
    vma = NULL;
    {
        unsigned long addr = 0;
        while ((vma = find_vma(mm, addr)) != NULL) {
            printk(KERN_INFO "VMA: 0x%lx - 0x%lx (size: %lu KB) flags: %lx\n",
                   vma->vm_start, vma->vm_end,
                   (vma->vm_end - vma->vm_start) / 1024,
                   vma->vm_flags);

            addr = vma->vm_end;
            if (addr == 0)
                break;
        }
    }
    
    up_read(&mm->mmap_lock);
    mmput(mm);
}

// Function to find VA->PA translation using get_user_pages_remote (6-argument variant)
static void find_page(unsigned long addr)
{
    struct mm_struct *mm;
    struct page *page = NULL;
    unsigned long phys_addr;
    int ret;
    
    mm = get_task_mm(current);
    if (!mm) {
        printk(KERN_ERR "mmaneg: Failed to get mm_struct for current process\n");
        return;
    }
    
    down_read(&mm->mmap_lock);
    
    // Check if address is valid
    if (!find_vma(mm, addr)) {
        printk(KERN_INFO "mmaneg: Invalid address 0x%lx - not in any VMA\n", addr);
        goto out;
    }
    
    ret = get_user_pages_remote(mm, addr, 1, 0, &page, NULL);
    if (ret <= 0) {
        printk(KERN_INFO "mmaneg: Could not get user page at address 0x%lx\n", addr);
        goto out;
    }
    
    // Calculate physical address
    phys_addr = page_to_phys(page) + (addr & ~PAGE_MASK);
    printk(KERN_INFO "mmaneg: VA 0x%lx -> PA 0x%lx\n", addr, phys_addr);
    
    put_page(page);
    
out:
    up_read(&mm->mmap_lock);
    mmput(mm);
}

// Function to write a value to a given address
static void write_val(unsigned long addr, unsigned long val)
{
    struct mm_struct *mm;
    int ret;
    
    mm = get_task_mm(current);
    if (!mm) {
        printk(KERN_ERR "mmaneg: Failed to get mm_struct for current process\n");
        return;
    }
    
    down_read(&mm->mmap_lock);
    
    // Check if address is valid
    if (!find_vma(mm, addr)) {
        printk(KERN_INFO "mmaneg: Invalid address 0x%lx\n", addr);
        goto out;
    }
    
    ret = access_process_vm(current, addr, &val, sizeof(val), FOLL_WRITE);
    if (ret != sizeof(val)) {
        printk(KERN_INFO "mmaneg: Failed to write value at 0x%lx\n", addr);
    } else {
        printk(KERN_INFO "mmaneg: Wrote 0x%lx at 0x%lx\n", val, addr);
    }
    
out:
    up_read(&mm->mmap_lock);
    mmput(mm);
}

// Handle commands written to /proc/mmaneg
static ssize_t mmaneg_write(struct file *file, const char __user *buffer,
                            size_t count, loff_t *pos)
{
    char *kbuf;
    char *cmd, *arg1_str, *arg2_str;
    unsigned long arg1, arg2;
    
    kbuf = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;
    
    if (count > BUFFER_SIZE - 1)
        count = BUFFER_SIZE - 1;
    if (copy_from_user(kbuf, buffer, count)) {
        kfree(kbuf);
        return -EFAULT;
    }
    
    kbuf[count] = '\0';
    if (kbuf[count - 1] == '\n')
        kbuf[count - 1] = '\0';
    
    // Parse command
    cmd = kbuf;
    arg1_str = strchr(kbuf, ' ');
    if (arg1_str) {
        *arg1_str = '\0';
        arg1_str++;
        
        arg2_str = strchr(arg1_str, ' ');
        if (arg2_str) {
            *arg2_str = '\0';
            arg2_str++;
        }
    }
    
    if (strcmp(cmd, "listvma") == 0) {
        list_vmas();
    } else if (strcmp(cmd, "findpage") == 0 && arg1_str) {
        if (kstrtoul(arg1_str, 0, &arg1) == 0)
            find_page(arg1);
        else
            printk(KERN_INFO "mmaneg: Invalid address format\n");
    } else if (strcmp(cmd, "writeval") == 0 && arg1_str && arg2_str) {
        if (kstrtoul(arg1_str, 0, &arg1) == 0 &&
            kstrtoul(arg2_str, 0, &arg2) == 0)
        {
            write_val(arg1, arg2);
        } else {
            printk(KERN_INFO "mmaneg: Invalid address/value format\n");
        }
    } else {
        printk(KERN_INFO "mmaneg: Unknown command or missing args\n");
    }
    
    kfree(kbuf);
    return count;
}

static const struct proc_ops mmaneg_ops = {
    .proc_write = mmaneg_write,
};

static int __init mmaneg_init(void)
{
    our_proc_file = proc_create(PROCFS_NAME, 0220, NULL, &mmaneg_ops);
    if (!our_proc_file) {
        printk(KERN_ERR "mmaneg: Failed to create /proc/%s\n", PROCFS_NAME);
        return -ENOMEM;
    }
    printk(KERN_INFO "mmaneg: Module loaded\n");
    return 0;
}

static void __exit mmaneg_exit(void)
{
    proc_remove(our_proc_file);
    printk(KERN_INFO "mmaneg: Module unloaded\n");
}

module_init(mmaneg_init);
module_exit(mmaneg_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Egor Labareshnykh");
MODULE_DESCRIPTION("Memory management procfs module");