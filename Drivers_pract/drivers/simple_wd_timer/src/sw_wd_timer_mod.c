#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/timer.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("Simple watchdog timer practice");

#define WD_DEFAULT_TIMEOUT_MS 5000

struct sw_wd_timer{

	struct device *dev;

	struct timer_list timer_work;
	
	unsigned long timeout_jiffies;
	unsigned long last_pet_jiffies;

	atomic_t fired_count;
	atomic_t enabled;

	spinlock_t slock;
};

static void sw_wd_timer_ping(struct sw_wd_timer *wd)
{
	if (IS_ERR_OR_NULL(wd))
		return;
	
	if(!atomic_read(&wd->enabled))
		return;	
	
	mod_timer(&wd->timer_work,jiffies +  wd->timeout_jiffies);
}

static void sw_wd_timer_start(struct sw_wd_timer *wd)
{
	if(IS_ERR_OR_NULL(wd))
		return;
	
	atomic_set(&wd->enabled,1);
	
	mod_timer(&wd->timer_work, jiffies + wd->timeout_jiffies);	
	
	pr_info("%s:\t Watchdog timer enabled, timeout: %lu\n",__func__, wd->timeout_jiffies);
}

static void sw_wd_timer_stop(struct sw_wd_timer *wd)
{
	atomic_set(&wd->enabled,0);
	
	timer_delete_sync(&wd->timer_work);
	
	pr_info("%s:\t Watchdog timer disabled, timeout: %lu\n",__func__, wd->timeout_jiffies);
}


static void sw_wd_timer_work_func(struct timer_list *t)
{
	struct sw_wd_timer *swd = container_of(t,struct sw_wd_timer,timer_work);
	
	pr_info("%s:\t SW Watchdog interrupt fired\n",__func__);  
}

static int sw_wd_timer_probe(struct platform_device *pdev)
{
	struct sw_wd_timer *wd = devm_kzalloc(&pdev->dev,sizeof(*wd),GFP_KERNEL);

	if(!wd)
		return -ENOMEM;

	wd->dev = &pdev->dev;

	atomic_set(&wd->fired_count,0);

	wd->timeout_jiffies = msecs_to_jiffies(WD_DEFAULT_TIMEOUT_MS);

	wd->last_pet_jiffies = jiffies;

	timer_setup(&wd->timer_work, sw_wd_timer_work_func,0);

	platform_set_drvdata(pdev,wd);

	dev_info(&pdev->dev,"%s:\t Software watchdog module loaded succesfully\n",__func__);
	
	sw_wd_timer_start(wd);

	return 0;
}

static void sw_wd_timer_remove(struct platform_device *pdev)
{
	struct sw_wd_timer *wd = platform_get_drvdata(pdev);
	
	sw_wd_timer_stop(wd);

	timer_shutdown_sync(&wd->timer_work);

	dev_info(&pdev->dev,"%s:\t Successfully removed sw watchdog module\n",__func__);
}


static struct platform_driver sw_wd_timer_driver = {
	.probe = sw_wd_timer_probe,
	.remove = sw_wd_timer_remove,
	.driver = {
		.name = "sw_wd_timer_mod",
	},

};

module_platform_driver(sw_wd_timer_driver);









