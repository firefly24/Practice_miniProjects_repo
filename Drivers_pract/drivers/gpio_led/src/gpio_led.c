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
#include <linux/delay.h>
#include <linux/mutex.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("GPIO LED Driver practice");


/* Learning resourcess
Linux Internals (Sysfs, proc, Udev): https://www.youtube.com/watch?v=H5QbulRfXTE
Linux Device Driver model (Section 10) : https://www.udemy.com/share/103vLA3@am1gIgBcYFGJB5mE_ruHnOy3dzC5J02yaMfx3ChhMF6mwMqeDvQLgqcXl5D_729i/
sysfs device attributes:https://elixir.bootlin.com/linux/v6.15/source/include/linux/device.h
*/


/*
INVARIANTS-
- led_mode selects the policy/strategy for LED control
- Only the active controller policy is allowed to change the state of LED (led_state_enabled)
- worker functions for non-active led_modes must not touch led state
- gpio_led_set_state() is the only function that should toggle the real led_gpio HW state
- led mode should only be changed by using gpio_led_change_mode() api, instead of setting directly
*/

enum led_mode {
	GPIO_LED_MANUAL,
	GPIO_LED_BUTTON,
	GPIO_LED_BLINKING,
};

struct gpio_led_obj {

	struct device *dev;
	enum led_mode mode;
	
	// GPIOs required
	struct gpio_desc *led_gpio;			//led gpio descriptor
	struct gpio_desc *button_gpio;			// button press gpio descriptor
	
	int button_irq;					//IRQ number for button gpio
	bool led_state_enabled;				// led status tracking
	struct work_struct button_press_work;		// work item for button press
	struct work_struct commit_work;			// work item to commit HW state

	// for custom blinker 
	struct led_classdev blinker_led;		// led class device for blinker led control
	struct delayed_work blink_work;			// delayed workitem for custom blinker
	//bool custom_blink_en;				// Track custom blinker enabled/disable
	int blink_duration_ms;
	
	//features enabled
	bool sysfs_custom_blinker;			// If custom blinker control though sysfs is enabled
	bool button_press_control;			// If led control through button press is enabled
	
	spinlock_t state_lock;				// Protect SW state of gpio_led device
	struct mutex mode_lock;				// protect control mode changes
	struct mutex state_change_mutex;		// Serialize updation of HW state of led gpio
};


static void gpio_led_set_brightness(struct led_classdev *led_cdev,
				   enum led_brightness brightness);
				   
static void gpio_led_set_state(struct gpio_led_obj *priv, bool on );

static void gpio_led_change_mode(struct gpio_led_obj *priv, enum led_mode new_mode);


/*******************************************************************************************************/
/**************************************CUSTOM BLINKER RELATED FUNCTIONS*********************************/
/*******************************************************************************************************/

/*--------------Store and show methods for custom device attributes*/
static ssize_t custom_blink_show(struct device *dev,struct device_attribute *attr,char *buf);
static ssize_t custom_blink_store(struct device *dev, struct device_attribute *attr,const char *buf,size_t count);


/*Important! lookup difference between _ATTR and DEVICE_ATTR macros and use accordignly*/				   
//expands and creates: struct device_attribute dev_attr_custom_blink 
static DEVICE_ATTR(custom_blink,0664,custom_blink_show, custom_blink_store);

				   
/*-----------------blinker work-----------------------------------*/
static void custom_blinker_worker_func(struct work_struct *work)
{
	struct gpio_led_obj *priv = container_of(to_delayed_work(work), struct gpio_led_obj, blink_work);
	
	if (!priv->sysfs_custom_blinker)
		return;
	
	if (priv->mode != GPIO_LED_BLINKING)
	{
		pr_warn("%s:\t Ignore call in LED mode: %d\n",__func__, priv->mode);
		return;
	}
	
	gpio_led_set_state(priv,!priv->led_state_enabled);
	
	pr_debug("%s:\t led state: %s\n", __func__, priv->led_state_enabled?"ON":"OFF");
	
	//schedule work 
	if ((priv->blink_duration_ms >0) )
		schedule_delayed_work(&priv->blink_work,msecs_to_jiffies(priv->blink_duration_ms));
}


// Set-up custom blinker sysfs attribute to set custom time delay 
static bool create_custom_blinker_sysfs(struct gpio_led_obj *gpio_obj)
{

	//Create sysfs file for custom blinker
	int ret = device_create_file(gpio_obj->dev, &dev_attr_custom_blink);
	if (ret)
	{
		dev_err(gpio_obj->dev,"Custom blinker sysfs entry failed: %d\n",ret);
		goto failed_disable_feature;
	}
	else
	{
		// fill blinker leddev class struct
		gpio_obj->blinker_led.name = "blinker-led";
		gpio_obj->blinker_led.max_brightness = 1;
		gpio_obj->blinker_led.brightness_set = gpio_led_set_brightness;
		
		/*Instead of setting trigger here, I can use sysfs attribute "trigger" as well */
		//blinker_led.default_trigger = "timer";
		
		//register led device
		/*Using devm dev managed version of register api, it makes sure the device is removed automatically,
		when the owning module is removed using rmmod*/
		ret = devm_led_classdev_register(gpio_obj->dev,&(gpio_obj->blinker_led));
		if (ret)
		{
			device_remove_file(gpio_obj->dev, &dev_attr_custom_blink);
			pr_warn("%s:\t Failed to register blinker_led\n",__func__);
			goto failed_disable_feature;
		}
		INIT_DELAYED_WORK(&(gpio_obj->blink_work),custom_blinker_worker_func);
		pr_info("%s\t custom blinker setup successfully\n",__func__);
		return true;
	}

failed_disable_feature:
	gpio_obj->sysfs_custom_blinker = false;
	return false;
	
}


/*---------------------Method prints the value to user-------------*/
static ssize_t custom_blink_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	struct gpio_led_obj *priv = dev_get_drvdata(dev);
	
	if (!priv->sysfs_custom_blinker)
		return 0;
		
	pr_info("%s:\t Showing value to user: %d\n", __func__,priv->blink_duration_ms);
	return scnprintf(buf, PAGE_SIZE, "%d\n",priv->blink_duration_ms);
}


/*---------------This method stores the value input by user--------*/
static ssize_t custom_blink_store(struct device *dev, struct device_attribute *attr,const char *buf,size_t count)
{
	int temp;
	int ret;
	
	struct gpio_led_obj *priv = dev_get_drvdata(dev);
	
	if (!priv->sysfs_custom_blinker)
		return 0;
	
	ret = kstrtoint(buf,10,&temp);
	if (ret)
		return ret;
		
	priv->blink_duration_ms = temp;
	
	if (temp>0)
	{
		priv->blink_duration_ms = temp;
		
		//priv->mode = GPIO_LED_BLINKING;
		gpio_led_change_mode(priv, GPIO_LED_BLINKING);
		
		pr_info("%s:\t Setting blinker duration to: %d\n",__func__,priv->blink_duration_ms);
		
		//cancel stale delayed work
		cancel_delayed_work(&priv->blink_work);
		
		// reschedule with new timer
		schedule_delayed_work(&priv->blink_work,msecs_to_jiffies(priv->blink_duration_ms));	
	}
	else
	{
		priv->blink_duration_ms = 0;
		
		//priv->mode = GPIO_LED_MANUAL;
		gpio_led_change_mode(priv, GPIO_LED_MANUAL);
	}
	
	pr_info("%s:\t Value set by user: %d\n",__func__,temp);
	return count;
}


/*******************************************************************************************************/
/*******************BUTTON CONTROL RELATED FUNCTIONS****************************************************/
/*******************************************************************************************************/

// sysfs attribute for forcing button irq, since we are working in simulated env
static ssize_t button_irq_sim_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(button_irq_sim);

static void button_press_worker_func(struct work_struct *work)
{
	struct gpio_led_obj *priv = container_of(work, struct gpio_led_obj,button_press_work);
	
	if (!priv->button_press_control)
		return;
		
	if (priv->mode != GPIO_LED_BUTTON)
	{
		pr_warn("%s:\t Ignoring call in led mode :%d\n",__func__,priv->mode);
		return;
	}
	
	if (!IS_ERR_OR_NULL(priv->led_gpio))
		gpio_led_set_state(priv,!priv->led_state_enabled);	//toggle LED
		
	pr_info("%s:\t irq: %d, val: %d\n",__func__, priv->button_irq, priv->led_state_enabled);
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
	
	struct gpio_led_obj *priv = dev_id;
	
	if (priv->mode != GPIO_LED_BUTTON)
	{
		pr_warn("%s:\t Ignore call in led mode: %d\n",__func__, priv->mode);
		return IRQ_HANDLED;
	}

	
	if (IS_ERR_OR_NULL(priv->button_gpio))
		return IRQ_HANDLED;
	
	schedule_work(&priv->button_press_work);
	
	return IRQ_HANDLED;
}


static ssize_t button_irq_sim_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int temp; 
	int ret = kstrtoint(buf,10,&temp);
	
	struct gpio_led_obj *priv = dev_get_drvdata(dev);
	
	if (!priv->button_press_control || (priv->mode != GPIO_LED_BUTTON))
		return count;
	
	if (temp ==1)
	{
		if (priv->button_irq <=0)
		{
			pr_err("%s:\t IRQ is not initialized\n",__func__);
			return -EINVAL;
		}
		pr_info("%s:\t Manually triggering button IRQ\n",__func__);
		
		unsigned long irq_flags;
		
		// Simulation-only IRQ trigger for learning purpose on QEMU due to real life limitations
		local_irq_save(irq_flags);
		generic_handle_irq(priv->button_irq);
		local_irq_restore(irq_flags);
	}
	return count;
}


/*---------------------set-up button configuration ----------------*/
static bool button_controller_gpio_setup(struct gpio_led_obj* gpio_obj)
{	
	// add code for gpio control through button
	gpio_obj->button_gpio = devm_gpiod_get(gpio_obj->dev, "button", GPIOD_IN);
	
	if( IS_ERR_OR_NULL(gpio_obj->button_gpio) )
	{
		pr_warn("%s:\t Failed to enable button control for LED, error: %ld \n",__func__, 
								PTR_ERR(gpio_obj->button_gpio));
		goto failed_button_cleanup;
	}
	
	gpio_obj->button_irq = gpiod_to_irq(gpio_obj->button_gpio);
	if (gpio_obj->button_irq < 0 )
	{
		pr_warn("%s:\t Failed to get irq number for button\n",__func__);
		goto failed_button_cleanup;
	}
	
	if (device_create_file(gpio_obj->dev,&dev_attr_button_irq_sim))
	{
		pr_warn("%s:\t Cannot simulate button press\n",__func__);
		goto failed_button_cleanup;
	}
	
	INIT_WORK(&(gpio_obj->button_press_work) , button_press_worker_func);
	
	int ret = devm_request_threaded_irq(gpio_obj->dev,gpio_obj->button_irq,
					button_irq_hardirq,		// top half as dummy, for better understanding
					button_irq_thread_func,		// toggle led in bottom half
					 IRQF_ONESHOT| IRQF_TRIGGER_RISING,
					"button_gpio", gpio_obj);
					
	if (ret)
	{
		device_remove_file(gpio_obj->dev, &dev_attr_button_irq_sim);
		pr_warn("%s:\t Failed to setup IRQ for button\n",__func__);
		goto failed_button_cleanup;
	}
	pr_info("%s:\t IRQ (%d) for button setup successfully\n",__func__,gpio_obj->button_irq);
	return true;
	
failed_button_cleanup:
	gpio_obj->button_press_control = false;
	return false;
	
}


/*******************************************************************************************************/
/************************BASIC LED-GPIO DRIVER FUNCTIONS************************************************/
/*******************************************************************************************************/
// sysfs attribute for overriding  led mode
static ssize_t led_mode_override_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(led_mode_override);


/*-------------------- device-tree matching ----------------------*/
static const struct of_device_id gpio_led_of_match[] = {
	{.compatible = "practice,gpio-led"},
	{ },
};


// This commits the actual state to the HW
static void gpio_led_commit_hw_state(struct work_struct* work)
{
	struct gpio_led_obj *priv = container_of(work, struct gpio_led_obj,commit_work);
	
	mutex_lock(&priv->state_change_mutex);
	
	gpiod_set_value(priv->led_gpio,priv->led_state_enabled);
	
	mutex_unlock(&priv->state_change_mutex);

}

static void gpio_led_change_mode(struct gpio_led_obj *priv, enum led_mode new_mode)
{
	mutex_lock(&priv->mode_lock);
	if (priv->mode == new_mode)
		goto done;
		
	/*Teardown old mode first */
	if (priv->mode == GPIO_LED_BLINKING)
	{
		priv->blink_duration_ms = 0; // Disable blinking
		cancel_delayed_work_sync(&priv->blink_work); // cancel further work
	}
	else if (priv->mode == GPIO_LED_BUTTON)
	{
		cancel_work_sync(&priv->button_press_work);
	}
	else if (priv->mode == GPIO_LED_MANUAL)
	{
		pr_debug("%s:\t DO nothing for manual mode teardown\n",__func__);
	}
	
	//priv->mode = new_mode;
	gpio_led_set_state(priv,false); // turn off led until new mode controller takes effect

	// Apply new mode
	switch(new_mode)
	{
	
	case GPIO_LED_MANUAL:
		pr_info("%s:\t Now user progam can control led manually\n",__func__);
		break;
		
	case GPIO_LED_BLINKING:
		if (!priv->sysfs_custom_blinker)
			new_mode = GPIO_LED_MANUAL;
		pr_info("%s:\t Now user can trigger blinking through sysfs\n",__func__);
		break;
		
	case GPIO_LED_BUTTON:
		if (!priv->button_press_control)
			new_mode = GPIO_LED_MANUAL;
		pr_info("%s:\t Now user can toggle led through button press\n",__func__);
		break;
	default:
		break;
	}
	
	priv->mode = new_mode;
 done:
 	mutex_unlock(&priv->mode_lock);
}

static ssize_t led_mode_override_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct gpio_led_obj *priv = dev_get_drvdata(dev);
	enum led_mode newmode = GPIO_LED_MANUAL;
	
	if( sysfs_streq(buf,"blink") )
		newmode = GPIO_LED_BLINKING;
	
	else if (sysfs_streq(buf,"button") )
		newmode = GPIO_LED_BUTTON;
	
	else if (sysfs_streq(buf, "manual") )
		newmode = GPIO_LED_MANUAL;
	else
		dev_warn(priv->dev, "Invalid default mode: %s, setting led to manual control\n",buf);
		
	gpio_led_change_mode(priv, newmode);
	
	return count;
}


static void gpio_led_set_state(struct gpio_led_obj *priv, bool on )
{
	unsigned long flags;
	bool changed = false;
	
	spin_lock_irqsave(&priv->state_lock,flags);
	if (priv->led_state_enabled != on)
	{
		changed = true;
		priv->led_state_enabled = on;
	}
	spin_unlock_irqrestore(&priv->state_lock,flags);
	
	// If current caller did not change state, it must not touch the HW 
	if (!changed)
		return;
		
	schedule_work(&priv->commit_work);
}

// TODO: how to use change_mode api here instead of setting mode manually
static void parse_default_mode_dt(struct gpio_led_obj *priv)
{
	const char *default_mode;
	
	priv->mode = GPIO_LED_MANUAL;
	
	if ( device_property_read_string(priv->dev, "led-default-mode", &default_mode) )
		return;
		
	if (!strcmp(default_mode,"blink"))
		gpio_led_change_mode(priv,GPIO_LED_BLINKING);
	if (!strcmp(default_mode, "button"))
		gpio_led_change_mode(priv,GPIO_LED_BUTTON);	
		
	pr_debug("%s:\t Selected LED default mode: %s \n",__func__, default_mode);
}

/*-------------------------probe----------------------------------*/
static int gpio_led_probe(struct platform_device *pdev)
{
	int ret=0;
	struct device *dev = &(pdev->dev);
	
	struct gpio_led_obj *gpio_obj;
	
	gpio_obj = devm_kzalloc(dev,sizeof(*gpio_obj),GFP_KERNEL);
	
	if (!gpio_obj)
		return -ENOMEM;
	
	gpio_obj->dev = dev;
	
	platform_set_drvdata(pdev, gpio_obj);

	pr_info("%s:\t Probing gpio_led module\n",__func__);

	gpio_obj->led_gpio = devm_gpiod_get(dev,"led",GPIOD_OUT_LOW);
	if (IS_ERR(gpio_obj->led_gpio))
	{
		pr_err("%s:\t Failed to get led gpio, error:%ld \n",__func__, PTR_ERR(gpio_obj->led_gpio));
		return PTR_ERR(gpio_obj->led_gpio);
	}
	
	mutex_init(&gpio_obj->state_change_mutex);
	spin_lock_init(&gpio_obj->state_lock);
	mutex_init(&gpio_obj->mode_lock);
	
	INIT_WORK(&gpio_obj->commit_work, gpio_led_commit_hw_state);
	
	gpio_led_set_state(gpio_obj,false);
	
	// ADD DEFAULT MODE dt PARSING HERE
	parse_default_mode_dt(gpio_obj);
	
	// Enable sysfs override of led mode
	if (device_create_file(gpio_obj->dev,&dev_attr_led_mode_override))
	{
		pr_warn("%s:\t Cannot change mode through sysfs\n",__func__);
	}
	
	// enable blinker thouugh sysfs - optional feature
	gpio_obj->sysfs_custom_blinker = create_custom_blinker_sysfs(gpio_obj);
	
	// Enable button controller - optional feature
	gpio_obj->button_press_control = button_controller_gpio_setup(gpio_obj);
	
	pr_info("%s:\t gpio_led module probed successfully\n",__func__);
	
	// publish probed config 
	dev_info(dev, "GPIO LED ready (mode: %d features:[%s, %s] )\n",
			gpio_obj->mode,
			gpio_obj->sysfs_custom_blinker?"blinker":"",
			gpio_obj->button_press_control?"button":"");
	
	return ret;
}


/*--------------------set brightness ------------------------------*/

static void gpio_led_set_brightness(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{

	struct gpio_led_obj *priv = container_of(led_cdev, struct gpio_led_obj, blinker_led);
	
	if (priv->mode != GPIO_LED_MANUAL)
	{
		pr_warn("%s:\t Ignore call in led mode %d \n",__func__, priv->mode);
		return;
	}
	
	//int res = gpiod_set_value(priv->led_gpio,brightness?1:0);
	gpio_led_set_state(priv,brightness?true:false);
	
	
	//I changed this from pr_info to _debug, because it was flooding dmsg
	pr_debug("%s:\t Setting brightness to: %d\n",__func__,brightness?1:0);	
}


/*----------platform driver remove --------------------------------*/

static void gpio_led_remove(struct platform_device *pdev)
{
	pr_info("%s:\t Removing module \n",__func__);
	
	struct gpio_led_obj *priv = platform_get_drvdata(pdev);
	
	gpio_led_change_mode(priv,GPIO_LED_MANUAL);
	
	cancel_work_sync(&priv->commit_work);
	// Cancel work and cleanup for button control
	if (priv->button_press_control)
	{	
		//devm_free_irq(&pdev->dev, priv->button_irq,priv);
		cancel_work_sync(&priv->button_press_work);
		device_remove_file(&pdev->dev, &dev_attr_button_irq_sim);
	}
	
	//Cancel work and cleanup for custom_blinker
	if (priv->sysfs_custom_blinker)
	{
		cancel_delayed_work_sync(&priv->blink_work);
		device_remove_file(&pdev->dev, &dev_attr_custom_blink);
	}
	
	// Turn off led gpio
	if (!IS_ERR_OR_NULL(priv->led_gpio))
	{
		gpio_led_set_state(priv,false);
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
