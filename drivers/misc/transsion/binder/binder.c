#include <linux/spinlock.h>

#include <uapi/linux/android/binder.h>

#ifdef CONFIG_TRAN_TNEK_SUPPORT
#include <tk/tnek.h>
#endif

#ifdef CONFIG_TRAN_ANDROID_BINDER_BLOCK_MONITOR
#include <linux/signal.h>
#endif

#ifdef CONFIG_TRAN_TRANLOG
#include <linux/tranlog.h>
#endif


static void probe_android_vh_expand_cmd(int cmd, int *ret, void __user *ubuf);
static ssize_t binder_get_serverinfo(struct binder_trans_info *info);
static int check_binder_thread_locked(struct binder_thread *thread);

static void probe_android_vh_expand_cmd(int cmd, int *ret, void __user *ubuf)
{
    if(cmd == BINDER_GET_SERVER_INFO){
        struct binder_trans_info info;
	struct task_struct *tsk;
#ifdef CONFIG_TRAN_TRANLOG
        struct athena_msg *msg = NULL;
#endif
        if (copy_from_user(&info, ubuf, sizeof(info))) {
            ret =  -EFAULT;
            goto err;
        }
        if (info.kill) {
              rcu_read_lock();
	      tsk = find_task_by_vpid(info.pid);
	      if (tsk) {
	            do_send_sig_info(SIGKILL, SEND_SIG_FORCED, tsk, false);
	      }
              rcu_read_unlock();

	      pr_info("check proc %d name %s hang long time, restart it\n", info.pid, (tsk ? tsk->comm : "known"));
              #ifdef CONFIG_TRAN_TRANLOG
              msg = alloc_msg();
              msg_put_int(&msg, "THREAD_BLOCK_COUNT", 1);
              submit_msg(&msg, 1, SDD_BINDER_BLOCK, "binderthreadusedup");
              free_msg(&msg);
              #endif
                  break;
            }

            binder_get_serverinfo(&info);

            if (copy_to_user(ubuf, &info, sizeof(info))) {
                ret = -EFAULT;
		   goto err;
            }
	     break;

    }

}

static ssize_t binder_get_serverinfo(struct binder_trans_info *info)
{
    int prev_pid = 0;
    int cur_pid = 0;
    struct rb_node *n;
    struct binder_proc *proc;
    struct binder_thread *thread;
    int ret;
    int count = 0;
    struct task_struct * task;

    if (!info)
        goto err;

    if (!info->pid)
        goto out;

    mutex_lock(&binder_procs_lock);

    hlist_for_each_entry(proc, &binder_procs, proc_node) {
        if (info->pid == proc->pid && 0 == strcmp("binder", proc->context->name)) {
		mutex_unlock(&binder_procs_lock);

             binder_inner_proc_lock(proc);
		for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		    thread = rb_entry(n, struct binder_thread, rb_node);
                    //binder_inner_proc_unlock(proc);
                    cur_pid = check_binder_thread_locked(thread);

		    if (cur_pid > 0) {
			  ++count;
			  if (prev_pid == cur_pid || count >= 3) {
                              //binder_inner_proc_lock(proc);
                         break;
			  }
			  prev_pid = cur_pid;
		    }
                    //binder_inner_proc_lock(proc);
                }

		binder_inner_proc_unlock(proc);
		mutex_lock(&binder_procs_lock);
		break;
	  }
    }

    mutex_unlock(&binder_procs_lock);
out:
    info->pid = cur_pid;
    // get ppid info
        if (cur_pid > 0) {
        rcu_read_lock();
        task = find_task_by_vpid(cur_pid);
	  if (task && task->real_parent) {
	      info->ppid = task->real_parent->pid;
	  }
	  rcu_read_unlock();
    }
    pr_info("binder_get_serverinfo:cur_pid=%d, ppid=%d\n", cur_pid, info->ppid);
    return ret;
err:
    return -1;
}

static int check_binder_thread_locked(struct binder_thread *thread)
{
    struct binder_transaction *t;
    struct binder_proc *to_proc;

    t = thread->transaction_stack;
    while (t) {
    //protects @from, @to_proc, and @to_thread
    spin_lock(&t->lock);
        if (t->from == thread) {
            to_proc = t->to_proc;
            spin_unlock(&t->lock);
            if (to_proc && to_proc->pid > 0) {
                return to_proc->pid;
            }

            t = t->from_parent;
        } else if (t->to_thread == thread) {
            spin_unlock(&t->lock);
            t = t->to_parent;
        } else {
            spin_unlock(&t->lock);
            t = NULL;
        }
    }

    return 0;
}

static int __init init_binder(void) {
    int ret, ret_erri_line;
    ret = register_trace_android_vh_expand_cmd(
                        probe_android_vh_expand_cmd, NULL);
    if (ret) {
        ret_erri_line = __LINE__;
        goto failed;
    }

    failed:
        if (ret)
                pr_err("register hooks failed, ret %d line %d\n", ret, ret_erri_line);

        return ret;

}

static void  __exit exit_binder(void){

}

module_init(init_binder);
module_exit(exit_binder);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Transsion.");
MODULE_DESCRIPTION("Transsion binder");