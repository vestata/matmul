#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("matrix multiplication");
MODULE_VERSION("0.1");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define HAVE_PROC_OPS
#endif

#define MAT_SIZE 100

/* submatrix size for concurrent computation */
#define SUBMAT_SIZE 10

#define MATRIX_IOCTL_MAGIC 'm'
#define MATRIX_IOCTL_SET_A _IOW(MATRIX_IOCTL_MAGIC, 1, int)
#define MATRIX_IOCTL_SET_B _IOW(MATRIX_IOCTL_MAGIC, 2, int)
#define MATRIX_IOCTL_COMPUTE _IO(MATRIX_IOCTL_MAGIC, 3)

static int matrix_a[MAT_SIZE][MAT_SIZE];
static int matrix_b[MAT_SIZE][MAT_SIZE];
static int result[MAT_SIZE][MAT_SIZE];
static struct mutex matrix_mutex;
static struct completion computation_done; /* for synchronization */

static int worker_thread(void *data)
{
    int start_row = *(int *) data;
    int end_row = start_row + SUBMAT_SIZE;
    int i, j, k;

    for (i = start_row; i < end_row; ++i) {
        for (j = 0; j < MAT_SIZE; ++j) {
            result[i][j] = 0;
            for (k = 0; k < MAT_SIZE; ++k)
                result[i][j] += matrix_a[i][k] * matrix_b[k][j];
        }
    }

    complete(&computation_done);
    return 0;
}

static long matrix_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case MATRIX_IOCTL_SET_A:
        /* Copy user data to kernel buffer (matrix_a) */
        if (copy_from_user(matrix_a, (int *) arg, sizeof(matrix_a)))
            return -EFAULT;
        break;

    case MATRIX_IOCTL_SET_B:
        /* Copy user data to kernel buffer (matrix_b) */
        if (copy_from_user(matrix_b, (int *) arg, sizeof(matrix_b)))
            return -EFAULT;
        break;

    case MATRIX_IOCTL_COMPUTE: {
        int i;
        mutex_lock(&matrix_mutex);

        init_completion(&computation_done);

        /* Create worker threads for each submatrix */
        for (i = 0; i < MAT_SIZE; i += SUBMAT_SIZE) {
            int *thread_arg = kmalloc(sizeof(int), GFP_KERNEL);
            *thread_arg = i;
            kthread_run(worker_thread, thread_arg, "worker_thread");
        }

        /* Wait for all threads to complete */
        for (i = 0; i < MAT_SIZE; i += SUBMAT_SIZE)
            wait_for_completion(&computation_done);

        mutex_unlock(&matrix_mutex);
        break;
    }


    default:
        return -EINVAL;
    }

    return 0;
}

static ssize_t matrix_read(struct file *file,
                           char __user *buf,
                           size_t count,
                           loff_t *pos)
{
    if (*pos >= sizeof(result)) /* End of file */
        return 0;

    if (*pos + count > sizeof(result))
        count = sizeof(result) - *pos;

    if (copy_to_user(buf, (char *) result + *pos, count))
        return -EFAULT;

    *pos += count;
    return count;
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops matrix_fops = {
    .proc_ioctl = matrix_ioctl,
    .proc_read = matrix_read,
};
#else
static const struct file_operations matrix_fops = {
    .unlocked_ioctl = matrix_ioctl,
    .read = matrix_read,
};
#endif

static struct proc_dir_entry *proc_entry = NULL;

static int __init matrix_init(void)
{
    mutex_init(&matrix_mutex);

    proc_entry = proc_create("matmul", 0666, NULL, &matrix_fops);
    if (!proc_entry) {
        printk(KERN_ALERT "Failed to create proc entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "Matrix multiplication module loaded\n");
    return 0;
}

static void __exit matrix_exit(void)
{
    if (proc_entry)
        proc_remove(proc_entry);

    mutex_destroy(&matrix_mutex);

    printk(KERN_INFO "Matrix multiplication module unloaded\n");
}

module_init(matrix_init);
module_exit(matrix_exit);
