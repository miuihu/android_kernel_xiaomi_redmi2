/*
 * drivers/leds/leds-aw2013.c
 *
 * Copyright (C) 2015 Balázs Triszka <balika011@protonmail.ch>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/leds.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/delay.h>

// Registers
#define AW2013_REG_RESET			0x00
#define AW2013_REG_GLOBAL_CONTROL		0x01
//#define AW2013_REG_LED_STATUS			0x02
#define AW2013_REG_LED_ENABLE			0x30
#define AW2013_REG_LED_CONFIG_BASE		0x31
#define AW2013_REG_LED_BRIGHTNESS_BASE		0x34
#define AW2013_REG_TIMER_BASE			0x37

//Bits
#define AW2013_RESET_MASK			0x55
#define AW2013_CONTROL_MOUDLE_ENABLE_MASK	0x01
#define AW2013_CONFIG_BLINK_MASK		(1 << 4)
#define AW2013_CONFIG_FADE_ON_MASK		(1 << 5)
#define AW2013_CONFIG_FADE_OFF_MASK		(1 << 6)

#define AW2013_CHIPID				0x33
#define AW2013_LED_RESET_DELAY			8

#define AW2013_GEN_TIMER_DATA(fade, stay) \
	((fade << 4) | stay)

#define aw2013_write(aw2013, reg, val) \
	i2c_smbus_write_byte_data(aw2013->i2c, reg, val)

#define aw2013_read(aw2013, reg) \
	i2c_smbus_read_byte_data(aw2013->i2c, reg)


struct aw2013_data
{
	struct i2c_client *i2c;
	struct regulator *vi2c;

	struct led_classdev cdev[3];	
};

static int aw2013_check_chipid(struct aw2013_data *aw2013)
{
	aw2013_write(aw2013, AW2013_REG_RESET, AW2013_RESET_MASK);
	usleep(AW2013_LED_RESET_DELAY);
	if (aw2013_read(aw2013, AW2013_REG_RESET) == AW2013_CHIPID)
		return 0;
	else
		return -EINVAL;
}

static void aw2013_brightness_set(struct aw2013_data *aw2013, u8 id, u8 brightness)
{
	if (id < 0 || id > 2)
	{
		dev_err(&aw2013->i2c->dev, "id is out of range (%d)\n", id);
		return;
	}

	dev_info(&aw2013->i2c->dev, "set brightness of led %d to %d\n", id, brightness);

	aw2013->cdev[id].brightness = brightness;

	aw2013_write(aw2013, AW2013_REG_LED_CONFIG_BASE + id, 3);
	aw2013_write(aw2013, AW2013_REG_LED_BRIGHTNESS_BASE + id, brightness);
}

static void aw2013_blink_set(struct aw2013_data *aw2013, u8 id, u8 blinking)
{
	if (id < 0 || id > 2)
	{
		dev_err(&aw2013->i2c->dev, "id is out of range (%d)\n", id);
		return;
	}

	dev_info(&aw2013->i2c->dev, "id: %d, blinking: %d\n", id, blinking);

	if(blinking)
		aw2013_write(aw2013, AW2013_REG_LED_CONFIG_BASE + id, aw2013_read(aw2013, AW2013_REG_LED_CONFIG_BASE + id) | AW2013_CONFIG_BLINK_MASK);
	else
		aw2013_write(aw2013, AW2013_REG_LED_CONFIG_BASE + id, aw2013_read(aw2013, AW2013_REG_LED_CONFIG_BASE + id) & ~AW2013_CONFIG_BLINK_MASK);
}

static u8 aw2013_blink_get(struct aw2013_data *aw2013, u8 id)
{
	if (id < 0 || id > 2)
	{
		dev_err(&aw2013->i2c->dev, "id is out of range (%d)\n", id);
		return 0;
	}

	return aw2013_read(aw2013, AW2013_REG_LED_CONFIG_BASE + id) & AW2013_CONFIG_BLINK_MASK ? 1 : 0;
}

#define AW2013_GEN_LED(id) \
static void aw2013_led_##id##_brightness_set(struct led_classdev *cdev, enum led_brightness brightness) \
{ \
	struct aw2013_data *aw2013 = container_of(cdev, struct aw2013_data, cdev[id]); \
\
	aw2013_brightness_set(aw2013, id, brightness); \
} \
\
static ssize_t aw2013_led_##id##_blink_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct led_classdev *led_cdev = dev_get_drvdata(dev); \
	struct aw2013_data *aw2013 = container_of(led_cdev, struct aw2013_data, cdev[id]); \
\
	return snprintf(buf, PAGE_SIZE, "%d\n", aw2013_blink_get(aw2013, id)); \
} \
\
static ssize_t aw2013_led_##id##_blink_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len) \
{ \
	struct led_classdev *led_cdev = dev_get_drvdata(dev); \
	struct aw2013_data *aw2013 = container_of(led_cdev, struct aw2013_data, cdev[id]); \
	ssize_t ret; \
	unsigned long blinking; \
\
	ret = kstrtoul(buf, 10, &blinking); \
	if (ret) \
		return ret; \
\
	aw2013_blink_set(aw2013, id, blinking); \
\
	return len; \
} \
\
static struct device_attribute dev_attr_led_##id##_blink = \
	__ATTR(blink, 0664, aw2013_led_##id##_blink_show, aw2013_led_##id##_blink_store); \
\
static struct attribute *aw2013_led_##id##_attributes[] = { \
	&dev_attr_led_##id##_blink.attr, \
	NULL, \
};

AW2013_GEN_LED(0)
AW2013_GEN_LED(1)
AW2013_GEN_LED(2)

static struct attribute_group aw2013_leds_attr_group[] = {
	{ .attrs = aw2013_led_0_attributes },
	{ .attrs = aw2013_led_1_attributes },
	{ .attrs = aw2013_led_2_attributes },
};

static int aw2013_register_leds(struct aw2013_data* aw2013)
{
	int i, ret;

#define AW2013_GEN_CDEV(id, led_name) \
	aw2013->cdev[id].name = led_name; \
	aw2013->cdev[id].brightness = LED_OFF; \
	aw2013->cdev[id].max_brightness = 255; \
	aw2013->cdev[id].brightness_set = aw2013_led_##id##_brightness_set; \
	aw2013->cdev[id].flags |= LED_CORE_SUSPENDRESUME;

	AW2013_GEN_CDEV(0, "red")
	AW2013_GEN_CDEV(1, "green")
	AW2013_GEN_CDEV(2, "blue")

	for (i = 0; i < 3; ++i)
	{
		ret = led_classdev_register(&aw2013->i2c->dev, &aw2013->cdev[i]);
		if (ret) {
			dev_err(&aw2013->i2c->dev, "failed to register led %i\n", i);
			return ret;
		}

		ret = sysfs_create_group(&aw2013->cdev[i].dev->kobj, &aw2013_leds_attr_group[i]);
		if (ret) {
			dev_err(&aw2013->i2c->dev, "led sysfs\n");
			return ret;
		}

		aw2013_write(aw2013, AW2013_REG_LED_ENABLE, aw2013_read(aw2013, AW2013_REG_LED_ENABLE) | 1 << i);
	}

	return 0;
}

static int aw2013_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	int ret = 0;
	struct aw2013_data* aw2013;

	aw2013 = devm_kzalloc(&i2c->dev, sizeof(struct aw2013_data), GFP_KERNEL);
	if (!aw2013) {
		dev_err(&i2c->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c, aw2013);
	aw2013->i2c = i2c;

	aw2013->vi2c = devm_regulator_get(&i2c->dev, "vi2c");
	if (IS_ERR(aw2013->vi2c)) {
		dev_err(&i2c->dev, "Failed to get vi2c regulator\n");
		ret = PTR_ERR(aw2013->vi2c);
		goto free_aw2013;
	}

	regulator_set_voltage(aw2013->vi2c, 2800000, 2800000);
	ret = regulator_enable(aw2013->vi2c);
	if (ret) {
		dev_err(&i2c->dev, "Failed to enable vi2c regulator\n");
		goto put_vi2c;
	}

	ret = aw2013_check_chipid(aw2013);
	if (ret) {
		dev_err(&i2c->dev, "Failed to check chip id\n");
		goto dis_vi2c;
	}

	aw2013_write(aw2013, AW2013_REG_GLOBAL_CONTROL, AW2013_CONTROL_MOUDLE_ENABLE_MASK);

	aw2013_register_leds(aw2013);

	return 0;

dis_vi2c:
	regulator_disable(aw2013->vi2c);
put_vi2c:
free_aw2013:
	devm_kfree(&i2c->dev, aw2013);

	return ret;
}

static int aw2013_remove(struct i2c_client *i2c)
{
	return 0;
}

static const struct i2c_device_id aw2013_id[] = {
	{ "leds-aw2013", 0 },
	{ }
};

static struct of_device_id aw2013_match_table[] = {
	{ .compatible = "awinc,aw2013", },
	{ },
};

static struct i2c_driver aw2013_driver = {
	.probe = aw2013_probe,
	.remove = aw2013_remove,
	.id_table = aw2013_id,
	.driver = {
		.name = "leds-aw2013",
		.owner = THIS_MODULE,
		.of_match_table = aw2013_match_table,
	},
};

module_i2c_driver(aw2013_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Balázs Triszka <balika011@protonmail.ch>");
MODULE_DESCRIPTION("Awinc AW2013 Driver");
