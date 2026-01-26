#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/jiffies.h>
//#include <linux/platform_device.h>
//#include <linux/device.h>
#include <linux/timer.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("Simple watchdog timer practice");

#define WD_DEFAULT_TIMEOUT_MS 5000

struct sw_wd_timer{

	//struct device *dev;

	struct timer_list timer_work;
	
	unsigned long timeout_jiffies;
	unsigned long last_pet_jiffies;

	atomic_t ping_count;
	atomic_t is_active;

	//spinlock_t slock;
};

// writing the module for one instance only for now
static struct sw_wd_timer wd;

static void sw_wd_timer_ping(struct sw_wd_timer *swd)
{
	if (IS_ERR_OR_NULL(swd))
		return;
	
	if(!atomic_read(&swd->is_active))
		return;	
		
	atomic_add(1,&swd->ping_count);
	swd->last_pet_jiffies = jiffies;
	
	mod_timer(&swd->timer_work,jiffies +  swd->timeout_jiffies);
}

static void sw_wd_timer_start(struct sw_wd_timer *swd)
{
	if(IS_ERR_OR_NULL(swd))
		return;
	
	atomic_set(&swd->ping_count,0);
	swd->last_pet_jiffies = jiffies;
	
	atomic_set(&swd->is_active,1);
	
	mod_timer(&swd->timer_work, jiffies + swd->timeout_jiffies);	
	
	pr_info("%s:\t Watchdog timer enabled, timeout: %lu\n",__func__, swd->timeout_jiffies);
}

static void sw_wd_timer_stop(struct sw_wd_timer *swd)
{
	atomic_set(&swd->is_active,0);
	
	timer_delete_sync(&swd->timer_work);
	
	pr_info("%s:\t Watchdog timer disabled, timeout: %lu\n",__func__, swd->timeout_jiffies);
}


static void sw_wd_timer_work_func(struct timer_list *t)
{
	struct sw_wd_timer *swd = container_of(t,struct sw_wd_timer,timer_work);
	
	pr_info("%s:\t SW Watchdog interrupt fired, last pet time: %lu, time elapsed: %lu\n",
		__func__,
		swd->last_pet_jiffies,
		jiffies_to_msecs(jiffies - swd->last_pet_jiffies));  
	
	if (!atomic_read(&swd->is_active))
		return;
	
	panic("SW watchdog expired!");
}

static int __init sw_wd_timer_init(void)
{
	//struct sw_wd_timer *wd = devm_kzalloc(&pdev->dev,sizeof(*wd),GFP_KERNEL);

	//if(!wd)
	//	return -ENOMEM;

	//wd->dev = &pdev->dev;

	wd.timeout_jiffies = msecs_to_jiffies(WD_DEFAULT_TIMEOUT_MS);

	timer_setup(&wd.timer_work, sw_wd_timer_work_func,0);

	//platform_set_drvdata(pdev,wd);

	pr_info("%s:\t Software watchdog module loaded succesfully\n",__func__);
	
	sw_wd_timer_start(&wd);

	return 0;
}

static void __exit sw_wd_timer_exit(void)
{
	//struct sw_wd_timer *wd = platform_get_drvdata(pdev);
	
	sw_wd_timer_stop(&wd);

	timer_shutdown_sync(&wd.timer_work);

	pr_info("%s:\t Successfully removed sw watchdog module\n",__func__);
}

module_init(sw_wd_timer_init);
module_exit(sw_wd_timer_exit);

//module_platform_driver(sw_wd_timer_driver);









