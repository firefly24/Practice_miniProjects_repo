#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/leds.h>
#include <linux/device.h>
#include <linux/sprintf.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/delay.h>


/* Learning resourcess
Linux Internals (Sysfs, proc, Udev): https://www.youtube.com/watch?v=H5QbulRfXTE
Linux Device Driver model (Section 10) : https://www.udemy.com/share/103vLA3@am1gIgBcYFGJB5mE_ruHnOy3dzC5J02yaMfx3ChhMF6mwMqeDvQLgqcXl5D_729i/
sysfs device attributes:https://elixir.bootlin.com/linux/v6.15/source/include/linux/device.h
*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("GPIO LED Driver practice");

//Declare global variables
struct gpio_desc *led_gpio;

static struct led_classdev blinker_led;
struct gpio_desc *button_gpio;
unsigned int button_irq;

// led class device for blinker led
static struct led_classdev blinker_led;

// delayed workitem for custom blinker
static struct delayed_work blink_work;

// led status tracking
static bool led_status_en = false;

// Track custom blinker enabled/disabled
static bool custom_blink_en = false;


int custom_blink_dummy_val = 0;

static void gpio_led_set_brightness(struct led_classdev *led_cdev,
				   enum led_brightness brightness);
				   
/*--------------Store and show methods for custom device attributes*/
static ssize_t custom_blink_show(struct device *dev,struct device_attribute *attr,char *buf);
static ssize_t custom_blink_store(struct device *dev, struct device_attribute *attr,const char *buf,size_t count);
				   
/*Important! lookup difference between _ATTR and DEVICE_ATTR macros and use accordignly*/				   
//expands and creates: struct device_attribute dev_attr_custom_blink 
static DEVICE_ATTR(custom_blink,0664,custom_blink_show, custom_blink_store);

// sysfs attribute for forcing button irq, since we are working in simulated env
static ssize_t button_irq_sim_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(button_irq_sim);

/*-------------------- devicetree matching ----------------------*/
static const struct of_device_id gpio_led_of_match[] = {
	{.compatible = "practice,gpio-led"},
	{ },
};

/*-----------------blinker work-----------------------------------*/
static void custom_blinker_worker_func(struct work_struct *work)
{
	// toggle LED state
	led_status_en = !led_status_en;
	
	pr_debug("%s:\t led state: %s\n", __func__, led_status_en?"ON":"OFF");
	//set gpio value
	gpiod_set_value(led_gpio,led_status_en?1:0);
	
	//schedule work 
	if (custom_blink_en && (custom_blink_dummy_val >0) )
	{
		schedule_delayed_work(&blink_work,msecs_to_jiffies(custom_blink_dummy_val));
	}
}


/*-----------------------hard irq- button irq handler --------------*/
static irqreturn_t button_irq_hardirq(int button_irq,void *dev_id)
{
	
	// tells kernel to wake the botton half handler
	return IRQ_WAKE_THREAD;
}


/*-----------------------bottm half - button irq handler -----------*/
// thread_fn parameter signature is seen in manage.c kernel code
static irqreturn_t button_irq_thread_func(int button_irq, void *dev_id)
{
	pr_info("%s:\t Button press triggered\n", __func__);
	//msleep(20); // treat as debounce time
	
	if (IS_ERR_OR_NULL(button_gpio))
		return IRQ_HANDLED;
		
	//int val = gpiod_get_value(button_gpio);
	
	//toggle led
	led_status_en = !led_status_en;
	if (!IS_ERR_OR_NULL(led_gpio))
		gpiod_set_value(led_gpio,led_status_en?1:0);
		
	pr_info("%s:\t irq: %d, val: %d\n",__func__, button_irq,led_status_en);
	
	return IRQ_HANDLED;
}

static ssize_t button_irq_sim_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int temp; 
	int ret = kstrtoint(buf,10,&temp);
	
	if (temp ==1)
	{
		
		if (button_irq <=0)
		{
			pr_err("%s:\t IRQ is not initialized\n",__func__);
			return -EINVAL;
		}
		
		
		pr_info("%s:\t Manually triggering button IRQ\n",__func__);
		
		/*
		if (!irq_to_desc(button_irq))
		{
			pr_err("%s:\t Unable to find IRQ descriptor\n",__func__);
			return -EINVAL;
		}
		*/
		unsigned long irq_flags;
		
		local_irq_save(irq_flags);
		generic_handle_irq(button_irq);
		local_irq_restore(irq_flags);
	}
	return count;
}

/*-------------------------probe----------------------------------*/
static int gpio_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &(pdev->dev);

	pr_info("%s:\t Probing gpio_led module\n",__func__);

	led_gpio = devm_gpiod_get(dev,"led",GPIOD_OUT_LOW);
	if (IS_ERR(led_gpio))
	{
		pr_err("%s:\t Failed to get led gpio, error:%ld \n",__func__, PTR_ERR(led_gpio));
		return PTR_ERR(led_gpio);
	}
		
	led_status_en = false;
	
	gpiod_set_value(led_gpio,0);
	
	//Create sysfs file for custom blinker
	ret = device_create_file(&(pdev->dev), &dev_attr_custom_blink);
	if (ret)
	{
		dev_err(&pdev->dev,"Custom blinker sysfs entry failed: %d\n",ret);
	}
	else
	{
		// fill blinker leddev class struct
		blinker_led.name = "blinker-led";
		blinker_led.max_brightness = 1;
		blinker_led.brightness_set = gpio_led_set_brightness;
		
		/*Instead of setting trigger here, I can use sysfs attribute "trigger" as well */
		//blinker_led.default_trigger = "timer";
		
		//register led device
		/*Using devm dev managed version of register api, it makes sure the device is removed automatically,
		when the owning module is removed using rmmod*/
		ret = devm_led_classdev_register(dev,&blinker_led);
		if (ret <0)
		{
			pr_err("%s:\t Failed to register blinker_led\n",__func__);
			return ret;
		}

		INIT_DELAYED_WORK(&blink_work,custom_blinker_worker_func);
	}
	
	// add code for gpio control through button
	button_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
	if( IS_ERR_OR_NULL(button_gpio))
	{
		pr_err("%s:\t Failed to enable button control for LED, error: %ld \n",__func__, PTR_ERR(button_gpio));
		goto success_load;
	}
	
	button_irq = gpiod_to_irq(button_gpio);
	/*
	if (button_irq < 0 )
	{
		pr_err("%s:\t Failed to get irq for button\n",__func__);
		goto success_load;
	}
	*/
	
	ret = device_create_file(&(pdev->dev),&dev_attr_button_irq_sim);
	
	ret = devm_request_threaded_irq(dev,button_irq,
					button_irq_hardirq,		// top half as dummy, for better understanding
					//NULL,				// no top half for simulated button press
					button_irq_thread_func,		// toggle led in bottom half
					 IRQF_ONESHOT| IRQF_TRIGGER_RISING,
					"button_gpio", pdev);
					
	if (ret)
	{
		pr_err("%s:\t Failed to setup IRQ for button\n",__func__);
		goto success_load;
	}
	pr_info("%s:\t IRQ (%d) for button setup successfully\n",__func__,button_irq);
	
success_load:
	pr_info("%s:\t gpio_led module probed successfully\n",__func__);
	return 0;
}

/*--------------------set brightness ------------------------------*/
static void gpio_led_set_brightness(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	int res = gpiod_set_value(led_gpio,brightness?1:0);
	
	/*I changed this from pr_info to _debug, because it was flooding dmsg*/
	pr_debug("%s:\t Setting brightness to: %d\n",__func__,brightness?1:0);	
}

/*---------------------Method prints the value to user-------------*/
static ssize_t custom_blink_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	pr_info("%s:\t Showing value to user: %d\n", __func__,custom_blink_dummy_val);
	return scnprintf(buf, PAGE_SIZE, "%d\n",custom_blink_dummy_val);
}

/*---------------This method stores the value input by user--------*/
static ssize_t custom_blink_store(struct device *dev, struct device_attribute *attr,const char *buf,size_t count)
{
	int temp;
		int ret;
	ret = kstrtoint(buf,10,&temp);
	if (ret)
		return ret;
		
	custom_blink_dummy_val = temp;
	
	
	if (temp>0)
	{
		custom_blink_dummy_val = temp;
		custom_blink_en = true;
		
		pr_info("%s:\t Setting blinker duration to: %d\n",__func__,custom_blink_dummy_val);
		
		//cancel stale delayed work
		cancel_delayed_work(&blink_work);
		
		// reschedule with new timer
		schedule_delayed_work(&blink_work,msecs_to_jiffies(custom_blink_dummy_val));	
	}
	else
	{
		custom_blink_en = false;
		custom_blink_dummy_val = 0;
		
		//cancel pending work if any
		cancel_delayed_work_sync(&blink_work);
		pr_info("%s:\t Disabling custom blinker\n",__func__);
		
		if (!IS_ERR_OR_NULL(led_gpio))
			gpiod_set_value(led_gpio,0);
	}
	
	pr_info("%s:\t Value set by user: %d\n",__func__,temp);
	return count;
}

/*----------platform driver remove --------------------------------*/
static void gpio_led_remove(struct platform_device *pdev)
{
	pr_info("%s:\t Removing module \n",__func__);
	
	// Remove custom sysfs attribute
	device_remove_file(&pdev->dev, &dev_attr_custom_blink);
		device_remove_file(&pdev->dev, &dev_attr_button_irq_sim);
	
	//Cancel any delayed work
	cancel_delayed_work_sync(&blink_work);
	
	// Turn off led gpio
	if (!IS_ERR_OR_NULL(led_gpio))
	{
		gpiod_set_value(led_gpio,0);
	}
}

/*----------------define the platform driver-----------------------*/
static struct platform_driver gpio_led_driver = {
    .probe = gpio_led_probe,
    .remove = gpio_led_remove,
    .driver = {
        .name = "gpio_led",
        .of_match_table = gpio_led_of_match,
    },
};

module_platform_driver(gpio_led_driver);
MODULE_DEVICE_TABLE(of,gpio_led_of_match);

/*
static int __init gpio_led_init(void)
{
    pr_info("%s:\tLoading Module\n",__func__);

	led_gpio = gpiod_get(NULL,"led",GPIOD_OUT_LOW);	

	if (IS_ERR(led_gpio)) {
		pr_err("%s:\tFailed to get GPIO, error:%ld \n",__func__, PTR_ERR(led_gpio));
		return 0;
	}

	pr_info("%s:\tLoaded module successfully\n",__func__);
	return 0;
}	

static void __exit gpio_led_exit(void)
{
    pr_info("%s:  Unloading module\n",__func__);
	
	if(!IS_ERR_OR_NULL(led_gpio))
		gpiod_put(led_gpio);

	pr_info("%s:\tUnloaded Gpio_led module successfully!\n",__func__);
}
*/


//module_init(gpio_led_init);
//module_exit(gpio_led_exit);