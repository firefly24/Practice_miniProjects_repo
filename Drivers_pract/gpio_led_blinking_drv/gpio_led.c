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

static void gpio_led_set_brightness(struct led_classdev *led_cdev,
				   enum led_brightness brightness);
				   
/*--------------Store and show methods for custom device attributes*/
static ssize_t custom_blink_show(struct device *dev,struct device_attribute *attr,char *buf);
static ssize_t custom_blink_store(struct device *dev, struct device_attribute *attr,const char *buf,size_t count);
				   
/*Important! lookup difference between _ATTR and DEVICE_ATTR macros and use accordignly*/				   
//expands and creates: struct device_attribute dev_attr_custom_blink 
static DEVICE_ATTR(custom_blink,0664,custom_blink_show, custom_blink_store);

int custom_blink_dummy_val = 0;


/*-------------------- devicetree matching ----------------------*/
static const struct of_device_id gpio_led_of_match[] = {
	{.compatible = "practice,gpio-led"},
	{ },
};

/*-------------------------probe----------------------------------*/
static int gpio_led_probe(struct platform_device *pdev)
{
	struct device *dev = &(pdev->dev);

	pr_info("%s:\t Probing gpio_led module\n",__func__);

	led_gpio = devm_gpiod_get(dev,NULL,GPIOD_OUT_LOW);
	if (IS_ERR(led_gpio))
	{
		pr_err("%s:\t Failed to get led gpio, error:%ld \n",__func__, PTR_ERR(led_gpio));
		return PTR_ERR(led_gpio);
	}
	
	//Create sysfs file for custom blinker
	device_create_file(&(pdev->dev), &dev_attr_custom_blink);
	
	// fill blinker leddev class struct
	blinker_led.name = "blinker-led";
	blinker_led.max_brightness = 1;
	blinker_led.brightness_set = gpio_led_set_brightness;
	
	/*Instead of setting trigger here, I can use sysfs attribute "trigger" as well */
	blinker_led.default_trigger = "timer";
	
	//register led device
	/*Using devm dev managed version of register api, it makes sure the device is removed automatically,
	when the owning module is removed using rmmod*/
	int ret = devm_led_classdev_register(dev,&blinker_led);
	if (ret <0)
	{
		pr_err("%s:\t Failed to register blinker_led\n",__func__);
		return ret;
	}

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
	kstrtoint(buf,10,&temp);
	custom_blink_dummy_val = temp;
	
	pr_info("%s:\t Value set by user: %d\n",__func__,temp);
	return count;
}

/*----------platform driver remove --------------------------------*/
static void gpio_led_remove(struct platform_device *pdev)
{
	pr_info("%s:\t Removing module \n",__func__);
	
	// Remove custom sysfs attribute
	device_remove_file(&pdev->dev, &dev_attr_custom_blink);
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

module_init(gpio_led_init);
module_exit(gpio_led_exit);
*/