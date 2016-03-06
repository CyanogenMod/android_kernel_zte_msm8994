/*******************************************************************************
*                                                                                *
*   File Name:    taos.c                                                      *
*   Description:   Linux device driver for Taos ambient light and         *
*   proximity sensors.                                     *
*   Author:         John Koshi                                             *
*   History:   09/16/2009 - Initial creation                          *
*           10/09/2009 - Triton version         *
*           12/21/2009 - Probe/remove mode                *
*           02/07/2010 - Add proximity          *
*                                                                                       *
********************************************************************************
*    Proprietary to Taos Inc., 1001 Klein Road #300, Plano, TX 75074        *
*******************************************************************************/
 // includes
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/delay.h>
#include <linux/i2c/tmd2772.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/poll.h>
#include <linux/wakelock.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#define LOG_TAG "SENSOR_ALS_PROX"
#define DEBUG_ON //DEBUG SWITCH

#define SENSOR_LOG_FILE__ strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/')+1) : __FILE__

#ifdef  CONFIG_FEATURE_ZTEMT_SENSORS_LOG_ON
#define SENSOR_LOG_ERROR(fmt, args...) printk(KERN_ERR   "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #ifdef  DEBUG_ON
#define SENSOR_LOG_INFO(fmt, args...)  printk(KERN_INFO  "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)

#define SENSOR_LOG_DEBUG(fmt, args...) printk(KERN_DEBUG "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #else
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
    #endif

#else
#define SENSOR_LOG_ERROR(fmt, args...)
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
#endif

static dev_t const tmd2772_proximity_dev_t = MKDEV(MISC_MAJOR, 101);
static dev_t const tmd2772_light_dev_t     = MKDEV(MISC_MAJOR, 102);


DECLARE_WAIT_QUEUE_HEAD(waitqueue_read);//iVIZM

struct ReadData { //iVIZM
    unsigned int data;
    unsigned int interrupt;
};
struct ReadData readdata[2];//iVIZM


// class structure for this device
struct class *taos_class;

// board and address info   iVIZM
struct i2c_board_info taos_board_info[] = {
    {I2C_BOARD_INFO(TAOS_DEVICE_ID, TAOS_DEVICE_ADDR2),},
};

unsigned short const taos_addr_list[2] = {TAOS_DEVICE_ADDR2, I2C_CLIENT_END};//iVIZM

// client and device
struct i2c_client *my_clientp;
struct i2c_client *bad_clientp[TAOS_MAX_NUM_DEVICES];

static struct class         *proximity_class;
static struct class         *light_class;


//iVIZM
static char pro_buf[4]; //iVIZM
static int mcount = 0; //iVIZM
static unsigned int als_poll_delay = 1000;
static unsigned int prox_debug_delay_time = 0;
u16 status = 0;
static int als_poll_time_mul  = 1;
static unsigned char reg_addr = 0;
static bool wakeup_from_sleep = false;

// ZTEMT ADD by zhubing
// modify for input filter the same data
static int last_proximity_data = -1;
//static int last_als_data       = -1;
// ZTEMT ADD by zhubing END


static const struct i2c_device_id tmd2772_idtable_id[] = {
	{ "ams,ams-sensor", 0 },
	{ },
};

static struct of_device_id of_tmd2772_idtable[] = {
	{ .compatible = "ams,ams-sensor",},
	{}
};

MODULE_DEVICE_TABLE(i2c, tmd2772_idtable);

struct i2c_driver tmd2772_driver = {
	.driver = {
		.name = "ams-sensor-tmd2772",
        .of_match_table = of_tmd2772_idtable,
		//.pm = NULL,
	},
	.id_table = tmd2772_idtable_id,
	.probe = tmd2772_probe,
	.remove = tmd2772_remove,
#ifdef CONFIG_PM_SLEEP //by clli2
    .resume = taos_resume,
    .suspend = taos_suspend,
#endif

};

// device configuration by clli2
struct taos_data *taos_datap;
struct taos_cfg *taos_cfgp;
static u16 als_time_param = 41;
static u16 scale_factor_param_prox = 6;
static u16 scale_factor_param_als = 6;
static u16 gain_trim_param = 512;          //NULL
static u8 filter_history_param = 3;        //NULL
static u8 filter_count_param = 1;          //NULL
/* gain_param  00--1X, 01--8X, 10--16X, 11--120X
 */
static u8 gain_param = 0;                  //same as prox-gain_param 1:0 8X
static u16 prox_calibrate_hi_param = 500;
static u16 prox_calibrate_lo_param = 330;
static u16 prox_threshold_hi_param = PROX_DEFAULT_THRESHOLD_HIGH;
static u16 prox_threshold_lo_param = PROX_DEFAULT_THRESHOLD_LOW;
static u16 als_threshold_hi_param  = 3000;
static u16 als_threshold_lo_param  = 10;
static u8  prox_int_time_param     = 0xF0;//0xCD; // time of the ALS ADC TIME, TIME = (255 - prox_int_time_param) * 2.72ms
static u8  prox_adc_time_param     = 0xFF; // time of the PRO ADC TIME, TIME = (255 - prox_int_time_param) * 2.72ms
static u8  prox_wait_time_param    = 0xFF; // time of the    Wait TIME, TIME = (255 - prox_int_time_param) * 2.72ms
/*7~4->pls,3~0->als*/
static u8  prox_intr_filter_param  = 0x33; // Int filter, Bit7--Bit4:PROX  Bit3--Bit0:ALS
static u8  prox_config_param       = 0x00; // wait long time disable
/*pulse/62.5Khz  less  32 recommand*/
static u8  prox_pulse_cnt_param    = PROX_LED_PULSE_CNT; //PROX LED pluse count to send for each measure 0x00--0xff:0--255
/* 7:6 11->100ma        00->12.5ma
   5:4 01->ch0          10->ch1    11->both
   1:0(als gain ctrol)  1X 8X 16X 128X        */
static u8  prox_gain_param = 0x20;   //50ma     8X
static u8  prox_config_offset_param  = 0x0;
// prox info
struct taos_prox_info prox_cal_info[20];
struct taos_prox_info prox_cur_info;
struct taos_prox_info *prox_cur_infop = &prox_cur_info;
static struct timer_list prox_poll_timer;
static int device_released = 0;
static u16 sat_als = 0;
static u16 sat_prox = 0;



// device reg init values
u8 taos_triton_reg_init[16] = {0x00,0xFF,0XFF,0XFF,0X00,0X00,0XFF,0XFF,0X00,0X00,0XFF,0XFF,0X00,0X00,0X00,0X00};

// lux time scale
struct time_scale_factor  {
    u16 numerator;
    u16 denominator;
    u16 saturation;
};
struct time_scale_factor TritonTime = {1, 0, 0};
struct time_scale_factor *lux_timep = &TritonTime;

// gain table
u8 taos_triton_gain_table[] = {1, 8, 16, 120};

// lux data
struct lux_data {
    u16 ratio;
    u16 clear;
    u16 ir;
};
struct lux_data TritonFN_lux_data[] = {
    { 9830,  8320,  15360 },
    { 12452, 10554, 22797 },
    { 14746, 6234,  11430 },
    { 17695, 3968,  6400  },
    { 0,     0,     0     }
};
struct lux_data *lux_tablep = TritonFN_lux_data;
static int lux_history[TAOS_FILTER_DEPTH] = {-ENODATA, -ENODATA, -ENODATA};//iVIZM

static int taos_get_data(void);


static void taos_irq_ops(bool enable, bool flag_sync)
{
    if (enable == taos_datap->irq_enabled)
    {
        SENSOR_LOG_INFO("doubule %s irq, retern here\n",enable? "enable" : "disable");
        return;
    }
    else
    {
        taos_datap->irq_enabled  = enable;
    }

    if (enable)
    {
        enable_irq(taos_datap->client->irq);
    }
    else
    {
        if (flag_sync)
        {
            disable_irq(taos_datap->client->irq);

        }
        else
        {
            disable_irq_nosync(taos_datap->client->irq);
        }
    }

    //SENSOR_LOG_INFO("%s irq \n",enable? "enable" : "disable");
}


static ssize_t attr_set_prox_led_pluse_cnt(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_pulse_cnt_param = val;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_pulse_cnt = prox_pulse_cnt_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0E), taos_cfgp->prox_pulse_cnt))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the prox_pulse_cnt reg\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_prox_led_pluse_cnt(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_led_pluse_cnt is %d\n", taos_cfgp->prox_pulse_cnt);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_als_adc_time(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_int_time_param = 255 - val;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_int_time = prox_int_time_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x01), taos_cfgp->prox_int_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the als_adc_time reg\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_als_adc_time(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "als_adc_time is 2.72 * %d ms\n", 255 - taos_cfgp->prox_int_time);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_prox_adc_time(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_adc_time_param = 255 - val;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_adc_time = prox_adc_time_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x02), taos_cfgp->prox_adc_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the prox_adc_time reg\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_prox_adc_time(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_adc_time is 2.72 * %d ms\n", 255 - taos_cfgp->prox_adc_time);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_wait_time(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_wait_time_param = 255 - val;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_wait_time = prox_wait_time_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x03), taos_cfgp->prox_wait_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the wait_time reg\n");
        }

    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_wait_time(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "wait_time is 2.72 * %d ms\n", 255 - taos_cfgp->prox_wait_time);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_prox_led_strength_level(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>4 || val<=0)
    {
        SENSOR_LOG_ERROR("input error, please input a number 1~~4");
    }
    else
    {
        val = 4 - val;
        prox_gain_param = (prox_gain_param & 0x3F) | (val<<6);

        if (NULL!=taos_cfgp)
        {
            taos_cfgp->prox_gain = prox_gain_param;
            if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
            {
                SENSOR_LOG_ERROR("failed to write the prox_led_strength reg\n");
            }
        }
        else
        {
            SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
        }
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_prox_led_strength_level(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    char *p_led_strength[4] = {"100", "50", "25", "12.5"};
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_led_strength is %s mA\n", p_led_strength[(taos_cfgp->prox_gain) >> 6]);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_set_als_gain(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>4 || val<=0)
    {
        SENSOR_LOG_ERROR("input error, please input a number 1~~4");
    }
    else
    {
        val = val-1;
        prox_gain_param = (prox_gain_param & 0xFC) | val;
        gain_param      = prox_gain_param & 0x03;

        if (NULL!=taos_cfgp)
        {
            taos_cfgp->gain      = gain_param;
            taos_cfgp->prox_gain = prox_gain_param;
            if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
            {
                SENSOR_LOG_ERROR("failed to write the prox_led_strength reg\n");
            }
        }
        else
        {
            SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
        }
    }


    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_als_gain(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    u8 als_gain[4] = {1, 8, 16, 120};
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "als gain is x%d\n", als_gain[taos_cfgp->prox_gain & 0x03]);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_prox_debug_delay(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_debug_delay_time =  val;

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_prox_debug_delay(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_debug_delay_time is %d\n", prox_debug_delay_time);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_debug_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val)
    {
        taos_datap->prox_debug = true;
    }
    else
    {
        taos_datap->prox_debug = false;
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_prox_debug_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_debug is %s\n", taos_datap->prox_debug? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_calibrate_start_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "flag_prox_calibrate_startis %s\n", taos_datap->prox_debug? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_offset_cal_start_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "flag_prox_offset_cal_start is %s\n", taos_datap->prox_debug? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_phone_is_sleep_store(struct device *dev, struct device_attribute *attr,
                                            const char *buf, size_t size)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	unsigned int recv;
    int ret = kstrtouint(buf, 10, &recv);
    if (ret)
    {
        SENSOR_LOG_ERROR("input error\n");
        return -EINVAL;
    }

    mutex_lock(&chip->lock);
    if (recv==chip->phone_is_sleep)
    {
        SENSOR_LOG_INFO("double %s phone_is_sleep\n",recv? "enable" : "false");
    }
    else
    {
        chip->phone_is_sleep = recv;
        SENSOR_LOG_INFO("success %s phone_is_sleep\n",recv? "enable" : "false");
    }
    mutex_unlock(&chip->lock);
	return size;
}

static ssize_t attr_prox_phone_is_sleep_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *chip = dev_get_drvdata(dev);
    SENSOR_LOG_INFO("prox calibrate is %s\n",chip->phone_is_sleep? "true" : "false");
	return snprintf(buf, PAGE_SIZE, "prox calibrate is %s\n\n", chip->phone_is_sleep? "true" : "false");
}

static ssize_t attr_prox_prox_wakelock_store(struct device *dev, struct device_attribute *attr,
                                            const char *buf, size_t size)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	unsigned int recv;
    int ret = kstrtouint(buf, 10, &recv);
    if (ret)
    {
        SENSOR_LOG_ERROR("input error\n");
        return -EINVAL;
    }

    mutex_lock(&chip->lock);
    if (recv)
    {
        taos_wakelock_ops(&(chip->proximity_wakelock),true);
    }
    else
    {
    	 //cancel_delayed_work_sync(&chip->prox_unwakelock_work);
	hrtimer_cancel(&taos_datap->prox_unwakelock_timer);
	taos_wakelock_ops(&(chip->proximity_wakelock),false);
    }
    mutex_unlock(&chip->lock);
	return size;
}

static ssize_t attr_prox_prox_wakelock_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *chip = dev_get_drvdata(dev);
    SENSOR_LOG_INFO("proximity_wakelock is %s\n",chip->proximity_wakelock.locked ? "true" : "false");
	return snprintf(buf, PAGE_SIZE, "proximity_wakelock is %s\n",chip->proximity_wakelock.locked ? "true" : "false");
}

static ssize_t attr_prox_offset_cal_spy_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val)
    {
        taos_datap->prox_spy_enable = true;
    }
    else
    {
        taos_datap->prox_spy_enable = false;
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_prox_offset_cal_spy_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "prox_spy_enable is %s\n", taos_datap->prox_spy_enable? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_datap is NULL\n");
    }
	return strlen(buf);
}



//als
static ssize_t attr_set_als_debug(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val)
    {
        taos_datap->als_debug = true;
    }
    else
    {
        taos_datap->als_debug = false;
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_als_debug(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_debug is %s\n", taos_datap->als_debug? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_irq(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val)
    {
        taos_irq_ops(true, true);
    }
    else
    {
        taos_irq_ops(false, true);
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_irq(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "flag_irq is %s\n", taos_datap->irq_enabled? "true" : "false");
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_set_prox_calibrate(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    int val,ret;

	ret=kstrtouint(buf, 10, &val);
    SENSOR_LOG_ERROR("enter\n");
    if (ret<0)
    {
        return -EINVAL;
    }

    if (val>1)
    {
        taos_datap->prox_calibrate_times= val;
        taos_prox_calibrate();
    }
    else
    {
        SENSOR_LOG_ERROR("your input error, please input a number that bigger than 1\n");
    }

	return size;
}

static ssize_t attr_prox_thres_high_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>0)
    {
        prox_calibrate_hi_param = val;
    }
    else
    {
        SENSOR_LOG_ERROR("you input error, please input a number that bigger than 0\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_prox_thres_high_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_calibrate_hi_param is %d\n",prox_calibrate_hi_param);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_thres_low_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>0)
    {
        prox_calibrate_lo_param = val;
    }
    else
    {
        SENSOR_LOG_ERROR("you input error, please input a number that bigger than 0\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_prox_thres_low_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_calibrate_lo_param is %d\n",prox_calibrate_lo_param);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_thres_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_calibrate_lo_param is %d\n prox_calibrate_hi_param is %d\n",taos_cfgp->prox_threshold_lo,taos_cfgp->prox_threshold_hi);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}
static ssize_t attr_prox_thres_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	static long value;
	int rc;

	rc = kstrtol(buf, 10, &value);
	if (rc)
		return -EINVAL;
	mutex_lock(&taos_datap->lock);

	if (value==1)
    {
   		if( (rc=taos_read_cal_value(CAL_THRESHOLD))<0)
        {
            mutex_unlock(&taos_datap->lock);
            return -EINVAL;
        }
		else
		{
		    taos_datap->prox_thres_have_cal = true;
    		taos_cfgp->prox_threshold_hi = rc;

            taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi < taos_datap->prox_thres_hi_max) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_max;
            taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > taos_datap->prox_thres_hi_min) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_min;

            taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;

            taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo < taos_datap->prox_thres_lo_max) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_max;
            taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo > taos_datap->prox_thres_lo_min) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_min;

    		input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
    		input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
    		input_sync(taos_datap->p_idev);
    		SENSOR_LOG_ERROR("prox_th_high  = %d\n",taos_cfgp->prox_threshold_hi);
    		SENSOR_LOG_ERROR("prox_th_low   = %d\n",taos_cfgp->prox_threshold_lo);
		}
	}
	else
    {
		mutex_unlock(&taos_datap->lock);
		return -EINVAL;
	}

	mutex_unlock(&taos_datap->lock);

	return size;
}
static ssize_t attr_set_als_scale_factor_param_prox(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>0)
    {
        scale_factor_param_prox = val;
        if (NULL!=taos_cfgp)
        {
            taos_cfgp->scale_factor_prox = scale_factor_param_prox;
        }
        else
        {
            SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("you input error, please input a number that bigger than 0\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_get_als_scale_factor_param_prox(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        sprintf(buf, "als_scale_factor_param_prox is %d\n",taos_cfgp->scale_factor_prox);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_als_scale_factor_param_als(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (val>0)
    {
        scale_factor_param_als = val;
        if (NULL!=taos_cfgp)
        {
            taos_cfgp->scale_factor_als = scale_factor_param_als;
        }
        else
        {
            SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("you input error, please input a number that bigger than 0\n");
    }


    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_get_als_scale_factor_param_als(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        sprintf(buf, "als_scale_factor_param_als is %d\n",taos_cfgp->scale_factor_als);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_get_prox_threshold_high(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "%d", taos_cfgp->prox_threshold_hi);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_prox_threshold_high(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_threshold_hi = val;
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}



static ssize_t attr_get_prox_threshold_low(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "%d", taos_cfgp->prox_threshold_lo);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_prox_threshold_low(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_threshold_lo = val;
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_set_prox_offset(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    prox_config_offset_param = val;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_config_offset = prox_config_offset_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the prox_config_offset  reg\n");
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_prox_offset(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        return sprintf(buf, "prox_config_offset_param is %d\n", taos_cfgp->prox_config_offset);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_calibrate_result_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "%d", taos_datap->prox_calibrate_result);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}
static ssize_t attr_prox_offset_cal_result_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "%d", taos_datap->prox_offset_cal_result);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_thres_hi_max(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        SENSOR_LOG_ERROR("prox_thres_hi_max is %d\n",taos_datap->prox_thres_hi_max);
        return sprintf(buf, "%d", PROX_THRESHOLD_HIGH_MAX);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_thres_hi_min(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        SENSOR_LOG_ERROR("prox_thres_hi_min is %d\n",taos_datap->prox_thres_hi_min);
        return sprintf(buf, "%d", taos_datap->prox_thres_hi_min);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_data_safa_range_max_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
            SENSOR_LOG_ERROR("PROX_DATA_SAFE_RANGE_MAX is %d\n",PROX_DATA_SAFE_RANGE_MAX);

        return sprintf(buf, "%d", PROX_DATA_SAFE_RANGE_MAX);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_data_safa_range_min_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
                SENSOR_LOG_ERROR("PROX_DATA_SAFE_RANGE_MIN is %d\n",PROX_DATA_SAFE_RANGE_MIN);

        return sprintf(buf, "%d", PROX_DATA_SAFE_RANGE_MIN);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}
static ssize_t attr_chip_name_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "%s", taos_datap->chip_name);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}


static ssize_t attr_prox_data_max(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_datap)
    {
        return sprintf(buf, "%d", taos_datap->prox_data_max);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_set_reg_addr(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    reg_addr = val;

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_get_reg_addr(struct device *dev,
		struct device_attribute *attr,	char *buf)
{

    SENSOR_LOG_ERROR("enter\n");
    SENSOR_LOG_ERROR("reg_addr = 0x%02X\n",reg_addr);
	return strlen(buf);
    SENSOR_LOG_ERROR("exit\n");

}


static ssize_t attr_set_reg_data(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val;
    int ret;
    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

    if (100==reg_addr)
    {
        SENSOR_LOG_ERROR("reg addr error!\n");
    }
    else
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|reg_addr), val))) < 0)
        {
            SENSOR_LOG_ERROR("failed write reg\n");
        }
    }

    SENSOR_LOG_ERROR("exit\n");
	return size;
}


static ssize_t attr_get_reg_data(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    unsigned char i;
    if (TAOS_TRITON_REG_ALL == reg_addr)
    {
        for (i=0x00; i<=0x0F; i++)
        {
            i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | i));
            SENSOR_LOG_ERROR("reg[0x%02X] = 0x%02X",i,i2c_smbus_read_byte(taos_datap->client));
        }
        for (i=0x11; i<=0x19; i++)
        {
            i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | i));
            SENSOR_LOG_ERROR("reg[0x%02X] = 0x%02X",i,i2c_smbus_read_byte(taos_datap->client));
        }

        i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | 0x1F));
        SENSOR_LOG_ERROR("reg[0x1F] = 0x%02X",i2c_smbus_read_byte(taos_datap->client));
    }
    else
    {
        i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | reg_addr));
        SENSOR_LOG_ERROR("reg[0x%02X] = 0x%02X",reg_addr,i2c_smbus_read_byte(taos_datap->client));
    }

	return strlen(buf);
}

static ssize_t attr_get_prox_value(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        SENSOR_LOG_ERROR("get_prox_value\n");
        schedule_delayed_work(&taos_datap->prox_flush_work, msecs_to_jiffies(200));
	    return sprintf(buf, "%d\n", last_proximity_data%100000);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_get_als_value(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL!=taos_cfgp)
    {
        SENSOR_LOG_ERROR("get_prox_value\n");
        schedule_delayed_work(&taos_datap->als_poll_work, msecs_to_jiffies(200));
        return strlen(buf);
    }
    else
    {
        sprintf(buf, "taos_cfgp is NULL\n");
    }
	return strlen(buf);
}

///***************************************************************************************///
//light
static ssize_t attr_als_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_on);
}

static ssize_t attr_als_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

    mutex_lock(&chip->lock);

	if (value)
    {
        taos_sensors_als_poll_on();
    }
    else
    {
        taos_sensors_als_poll_off();
    }

    mutex_unlock(&chip->lock);

	return size;
}

static ssize_t attr_als_poll_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "als_poll_time = %d\n", als_poll_delay);
}

static ssize_t attr_als_poll_time_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long time;
	int rc;

	rc = kstrtoul(buf, 10, &time);
	if (rc)
		return -EINVAL;
	als_poll_delay = time;
	return size;
}



//prox
static ssize_t attr_prox_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->prox_on);
}

static ssize_t attr_prox_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	bool value;
	if (strtobool(buf, &value))
		return -EINVAL;
    mutex_lock(&chip->lock);
    SENSOR_LOG_INFO("enter\n");

	if (value)
    {
        taos_prox_on();

        if (taos_datap->prox_spy_enable && !taos_datap->prox_debug && taos_datap->prox_thres_have_cal)
        {
            taos_prox_spy(taos_datap);
        }
    }
	else
    {
        taos_prox_off();
    }
    SENSOR_LOG_INFO("exit\n");
    mutex_unlock(&chip->lock);

	return size;
}

static ssize_t attr_prox_init_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct taos_data *chip = dev_get_drvdata(dev);
	return sprintf(buf, "chip->init%d\n", chip->init);

}

static ssize_t attr_prox_init_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int value =1;
	int ret =0,err=1;
	ret = kstrtouint(buf, 10, &value);
	if (ret)
		return -EINVAL;
    mutex_lock(&taos_datap->lock);
    SENSOR_LOG_INFO("enter\n");

	if (value ==1)
    {
    	if((ret=taos_read_cal_value(PATH_PROX_OFFSET))>0)
        {
		    taos_cfgp->prox_config_offset = ret;
        }

        if((ret=taos_read_cal_value(PATH_PROX_UNCOVER_DATA))>0)
        {
            taos_datap->prox_uncover_data = ret;

            taos_datap->prox_thres_hi_min = taos_datap->prox_uncover_data + PROX_THRESHOLD_SAFE_DISTANCE;
            taos_datap->prox_thres_hi_max = taos_datap->prox_thres_hi_min + PROX_THRESHOLD_DISTANCE / 2;
            taos_datap->prox_thres_hi_max = (taos_datap->prox_thres_hi_max > PROX_THRESHOLD_HIGH_MAX) ? PROX_THRESHOLD_HIGH_MAX : taos_datap->prox_thres_hi_max;
            taos_datap->prox_thres_lo_min = taos_datap->prox_uncover_data + PROX_THRESHOLD_DISTANCE;
            taos_datap->prox_thres_lo_max = taos_datap->prox_uncover_data + PROX_THRESHOLD_DISTANCE * 2;

            SENSOR_LOG_ERROR("prox_uncover_data = %d\n", taos_datap->prox_uncover_data);
            SENSOR_LOG_ERROR("prox_thres_hi range is [%d--%d]\n", taos_datap->prox_thres_hi_min, taos_datap->prox_thres_hi_max);
            SENSOR_LOG_ERROR("prox_thres_lo range is [%d--%d]\n", taos_datap->prox_thres_lo_min, taos_datap->prox_thres_lo_max);
        }

    	if((ret=taos_read_cal_value(CAL_THRESHOLD))<0)
		{
		    SENSOR_LOG_ERROR("prox_init<0\n");
		    err=taos_write_cal_file(CAL_THRESHOLD,0);
			if(err<0)
			{
				SENSOR_LOG_ERROR("ERROR=%s\n",CAL_THRESHOLD);
				mutex_unlock(&taos_datap->lock);
				return -EINVAL;
			}
           	taos_datap->prox_thres_have_cal = false;
		}
		else
        {
            if (ret==0)
            {
                taos_datap->prox_thres_have_cal = false;
                SENSOR_LOG_ERROR("taos_prox_calibrate==1\n");
    		}
    		else
            {
                taos_datap->prox_thres_have_cal = true;
                taos_cfgp->prox_threshold_hi = ret;

                taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi < taos_datap->prox_thres_hi_max) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_max;
                taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > taos_datap->prox_thres_hi_min) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_min;
                taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;
                taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo < taos_datap->prox_thres_lo_max) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_max;
                taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo > taos_datap->prox_thres_lo_min) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_min;

                input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
                input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
                input_sync(taos_datap->p_idev);
                SENSOR_LOG_ERROR("taos_prox_init> 0\n");
    		}
        }
	}
	else
    {
		SENSOR_LOG_ERROR("ERROR=tmg399x_prox_init_store\n");
		mutex_unlock(&taos_datap->lock);
		return -EINVAL;
	}

    SENSOR_LOG_INFO("prox_threshold_hi = %d, prox_threshold_lo = %d\n",taos_cfgp->prox_threshold_hi,taos_cfgp->prox_threshold_lo);

    SENSOR_LOG_INFO("exit\n");
    mutex_unlock(&taos_datap->lock);

	return size;
}

static ssize_t attr_prox_offset_cal_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int value =1;
	int ret =0;
	ret = kstrtouint(buf, 10, &value);
	if (ret)
	{
		return -EINVAL;
	}
		mutex_lock(&taos_datap->lock);
		SENSOR_LOG_INFO("enter\n");

	if (value ==1)
    {
        taos_datap->prox_offset_cal_start = true;
        schedule_delayed_work(&taos_datap->prox_offset_cal_work, msecs_to_jiffies(0));
        mutex_unlock(&taos_datap->lock);
	}
	else
    {
        SENSOR_LOG_ERROR("input error\n");
       	mutex_unlock(&taos_datap->lock);
		return -EINVAL;
    }

	SENSOR_LOG_INFO("exit\n");
	return size;
}

static ssize_t attr_prox_offset_cal_verify_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL != taos_datap)
    {
        return sprintf(buf, "%d", taos_datap->prox_offset_cal_verify);
    }
    else
    {
        sprintf(buf, "taos_datap is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_offset_cal_verify_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val = 0;

    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

	taos_datap->prox_offset_cal_verify = val;

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static ssize_t attr_prox_calibrate_verify_show(struct device *dev,
		struct device_attribute *attr,	char *buf)
{
    if (NULL != taos_datap)
    {
        return sprintf(buf, "%d", taos_datap->prox_calibrate_verify);
    }
    else
    {
        sprintf(buf, "taos_datap is NULL\n");
    }
	return strlen(buf);
}

static ssize_t attr_prox_calibrate_verify_store(struct device *dev,
		struct device_attribute *attr,	const char *buf, size_t size)
{
    unsigned long val = 0;

    SENSOR_LOG_ERROR("enter\n");
    if (strict_strtoul(buf, 10, &val))
    {
        return -EINVAL;
    }

	taos_datap->prox_calibrate_verify = val;

    SENSOR_LOG_ERROR("exit\n");
	return size;
}

static struct device_attribute attrs_light[] = {
	__ATTR(enable,                         0640,   attr_als_enable_show,                       attr_als_enable_store),
    __ATTR(light_gain,                     0644,   attr_get_als_gain,                          attr_set_als_gain),
    __ATTR(light_debug,                    0644,   attr_get_als_debug,                         attr_set_als_debug),
    __ATTR(light_adc_time,                 0644,   attr_get_als_adc_time,                      attr_set_als_adc_time),
    __ATTR(light_scale_factor_param,       0644,   attr_get_als_scale_factor_param_als,        attr_set_als_scale_factor_param_als),
    __ATTR(prox_scale_factor_param,        0644,   attr_get_als_scale_factor_param_prox,       attr_set_als_scale_factor_param_prox),
    __ATTR(delay,                          0640,   attr_als_poll_time_show,                    attr_als_poll_time_store),
    __ATTR(light_value,                    0444,   attr_get_als_value,                         NULL),
};

static struct device_attribute attrs_prox[] = {
    __ATTR(chip_name,                      0440,   attr_chip_name_show,                        NULL),
	__ATTR(enable,                         0640,   attr_prox_enable_show,                      attr_prox_enable_store),
	__ATTR(prox_init,                      0640,   attr_prox_init_show,                        attr_prox_init_store),
	__ATTR(prox_led_pluse_cnt,             0644,   attr_get_prox_led_pluse_cnt,                attr_set_prox_led_pluse_cnt),
    __ATTR(prox_adc_time,                  0644,   attr_get_prox_adc_time,                     attr_set_prox_adc_time),
    __ATTR(prox_led_strength_level,        0644,   attr_get_prox_led_strength_level,           attr_set_prox_led_strength_level),
    __ATTR(prox_debug_delay,               0644,   attr_get_prox_debug_delay,                  attr_set_prox_debug_delay),
    __ATTR(prox_calibrate,                 0220,   NULL,                                       attr_set_prox_calibrate),
    __ATTR(prox_threshold_high,            0644,   attr_get_prox_threshold_high,               attr_set_prox_threshold_high),
    __ATTR(prox_threshold_low,             0644,   attr_get_prox_threshold_low,                attr_set_prox_threshold_low),
    __ATTR(prox_offset,                    0444,   attr_get_prox_offset,                       attr_set_prox_offset),
    __ATTR(prox_value,                     0444,   attr_get_prox_value,                        NULL),
    __ATTR(prox_calibrate_result,          0440,   attr_prox_calibrate_result_show,            NULL),
    __ATTR(prox_thres_param_high,          0640,   attr_prox_thres_high_show,                  attr_prox_thres_high_store),
    __ATTR(prox_thres_param_low,           0640,   attr_prox_thres_low_show,                   attr_prox_thres_low_store),
    __ATTR(prox_thres,                     0640,   attr_prox_thres_show,                       attr_prox_thres_store),
    __ATTR(prox_debug,                     0640,   attr_prox_debug_show,                       attr_prox_debug_store),
    __ATTR(prox_calibrate_start,           0640,   attr_prox_calibrate_start_show,             attr_prox_debug_store),
    __ATTR(prox_thres_max,                 0444,   attr_prox_thres_hi_max,                     NULL),
    __ATTR(prox_thres_min,                 0444,   attr_prox_thres_hi_min,                     NULL),
    __ATTR(prox_data_max,                  0444,   attr_prox_data_max,                         NULL),
    __ATTR(prox_phone_is_sleep,            0640,   attr_prox_phone_is_sleep_show,              attr_prox_phone_is_sleep_store),
    __ATTR(prox_wakelock,                  0640,   attr_prox_prox_wakelock_show,               attr_prox_prox_wakelock_store),
    __ATTR(reg_addr,                       0644,   attr_get_reg_addr,                          attr_set_reg_addr),
    __ATTR(reg_data,                       0644,   attr_get_reg_data,                          attr_set_reg_data),
    __ATTR(irq_status,                     0644,   attr_get_irq,                               attr_set_irq),
    __ATTR(wait_time,                      0644,   attr_get_wait_time,                         attr_set_wait_time),
    __ATTR(prox_offset_cal_start,          0640,   attr_prox_offset_cal_start_show,            attr_prox_debug_store),
    __ATTR(prox_offset_cal,                0640,   attr_get_prox_offset,                       attr_prox_offset_cal_store),
    __ATTR(prox_offset_cal_result,         0444,   attr_prox_offset_cal_result_show,           NULL),
    __ATTR(prox_data_safe_range_max,       0444,   attr_prox_data_safa_range_max_show,         NULL),
    __ATTR(prox_data_safe_range_min,       0444,   attr_prox_data_safa_range_min_show,         NULL),
    __ATTR(prox_offset_cal_verify,         0644,   attr_prox_offset_cal_verify_show,           attr_prox_offset_cal_verify_store),
    __ATTR(prox_calibrate_verify,          0644,   attr_prox_calibrate_verify_show,            attr_prox_calibrate_verify_store),
    __ATTR(prox_offset_cal_spy,            0644,   attr_prox_offset_cal_spy_show,              attr_prox_offset_cal_spy_store),
};

static int create_sysfs_interfaces_prox(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attrs_prox); i++)
		if (device_create_file(dev, attrs_prox + i))
			goto error;
	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(dev, attrs_prox + i);
	SENSOR_LOG_ERROR("Unable to create interface\n");
	return -1;
}

static int create_sysfs_interfaces_light(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attrs_light); i++)
		if (device_create_file(dev, attrs_light + i))
			goto error;
	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(dev, attrs_light + i);
	SENSOR_LOG_ERROR("Unable to create interface\n");
	return -1;
}


static void taos_wakelock_ops(struct taos_wake_lock *wakelock, bool enable)
{
    if (enable == wakelock->locked)
    {
        SENSOR_LOG_INFO("doubule %s %s, retern here\n",enable? "lock" : "unlock",wakelock->name);
        return;
    }

    if (enable)
    {
        wake_lock(&wakelock->lock);
    }
    else
    {
        wake_unlock(&wakelock->lock);
    }

    wakelock->locked = enable;

    SENSOR_LOG_INFO("%s %s \n",enable? "lock" : "unlock",wakelock->name);
}

static int taos_write_cal_file(char *file_path,unsigned int value)
{
    struct file *file_p;
    char write_buf[10];
	 mm_segment_t old_fs;
    int vfs_write_retval=0;
    if (NULL==file_path)
    {
        SENSOR_LOG_ERROR("file_path is NULL\n");

    }
       memset(write_buf, 0, sizeof(write_buf));
      sprintf(write_buf, "%d\n", value);
    file_p = filp_open(file_path, O_CREAT|O_RDWR , 0665);
    if (IS_ERR(file_p))
    {
        SENSOR_LOG_ERROR("[open file <%s>failed]\n",file_path);
        goto error;
    }
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    vfs_write_retval = vfs_write(file_p, (char*)write_buf, sizeof(write_buf), &file_p->f_pos);
    if (vfs_write_retval < 0)
    {
        SENSOR_LOG_ERROR("[write file <%s>failed]\n",file_path);
        goto error;
    }

    set_fs(old_fs);
    filp_close(file_p, NULL);

    return 1;

error:
    return -1;
}


static int taos_read_cal_value(char *file_path)
{
    struct file *file_p;
    int vfs_read_retval = 0;
    mm_segment_t old_fs;
    char read_buf[32];
    unsigned short read_value;

    if (NULL==file_path)
    {
        SENSOR_LOG_ERROR("file_path is NULL\n");
        goto error;
    }

    memset(read_buf, 0, 32);

    file_p = filp_open(file_path, O_RDONLY , 0);
    if (IS_ERR(file_p))
    {
        SENSOR_LOG_ERROR("[open file <%s>failed]\n",file_path);
        goto error;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    vfs_read_retval = vfs_read(file_p, (char*)read_buf, 16, &file_p->f_pos);
    if (vfs_read_retval < 0)
    {
        SENSOR_LOG_ERROR("[read file <%s>failed]\n",file_path);
        goto error;
    }

    set_fs(old_fs);
    filp_close(file_p, NULL);

    if (kstrtou16(read_buf, 10, &read_value) < 0)
    {
        SENSOR_LOG_ERROR("[kstrtou16 %s failed]\n",read_buf);
        goto error;
    }

    SENSOR_LOG_ERROR("[the content of %s is %s]\n", file_path, read_buf);

    return read_value;

error:
    return -1;
}

static void taos_irq_work_func(struct work_struct * work) //iVIZM
{
    int retry_times = 0;
    int ret;
    mutex_lock(&taos_datap->lock);
    SENSOR_LOG_INFO("enter\n");
    if (wakeup_from_sleep)
    {
        SENSOR_LOG_INFO(" wakeup_from_sleep = true\n");
        mdelay(50);
        wakeup_from_sleep = false;
    }

    for (retry_times=0; retry_times<=50; retry_times++)
    {
        ret = taos_get_data();
        if (ret >= 0)
        {
            break;
        }
        mdelay(20);
    }
    taos_interrupts_clear();

    hrtimer_cancel(&taos_datap->prox_unwakelock_timer);
    taos_datap->irq_work_status = false;
   // SENSOR_LOG_INFO("########  taos_irq_work_func enter   hrtimer_start #########\n");
    hrtimer_start(&taos_datap->prox_unwakelock_timer, ktime_set(3, 0), HRTIMER_MODE_REL);

   //  schedule_delayed_work(&taos_datap->prox_unwakelock_work, msecs_to_jiffies(1000));

    taos_irq_ops(true, true);
    SENSOR_LOG_INFO(" retry_times = %d\n",retry_times);
    mutex_unlock(&taos_datap->lock);
}

static void taos_flush_work_func(struct work_struct * work) //iVIZM
{
    taos_prox_threshold_set();
}

static irqreturn_t taos_irq_handler(int irq, void *dev_id) //iVIZM
{
    SENSOR_LOG_INFO("enter\n");
    taos_datap->irq_work_status = true;
    taos_irq_ops(false, false);
    taos_wakelock_ops(&(taos_datap->proximity_wakelock), true);
    if (0==queue_work(taos_datap->irq_work_queue, &taos_datap->irq_work))
    {
        SENSOR_LOG_INFO("schedule_work failed!\n");
    }
    SENSOR_LOG_INFO("exit\n");
    return IRQ_HANDLED;
}

static int taos_get_data(void)//iVIZM
{
    int ret = 0;

    ret = i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_STATUS));

    if (ret < 0)
    {
        SENSOR_LOG_ERROR("read TAOS_TRITON_STATUS failed\n");
        return ret;
    }
    else
    {
        ret = taos_prox_threshold_set();
    }
    return ret;
}


static int taos_interrupts_clear(void)//iVIZM
{
    int ret = 0;
    if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG|TAOS_TRITON_CMD_SPL_FN|0x07)))) < 0) {
        SENSOR_LOG_ERROR(" i2c_smbus_write_byte(2) failed in taos_work_func()\n");
        return (ret);
    }
    return ret;
}

static int taos_als_get_data(void)//iVIZM
{
    int ret = 0;
    u8 reg_val;
    int lux_val = 0;
    if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL)))) < 0) {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte failed in ioctl als_data\n");
        return (ret);
    }
    reg_val = i2c_smbus_read_byte(taos_datap->client);
    if ((reg_val & (TAOS_TRITON_CNTL_ADC_ENBL | TAOS_TRITON_CNTL_PWRON)) != (TAOS_TRITON_CNTL_ADC_ENBL | TAOS_TRITON_CNTL_PWRON))
        return -ENODATA;
    if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_STATUS)))) < 0) {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte failed in ioctl als_data\n");
        return (ret);
    }
    reg_val = i2c_smbus_read_byte(taos_datap->client);
    if ((reg_val & TAOS_TRITON_STATUS_ADCVALID) != TAOS_TRITON_STATUS_ADCVALID)
        return -ENODATA;

    if ((lux_val = taos_get_lux()) < 0)
    {
        SENSOR_LOG_ERROR("call to taos_get_lux() returned error %d in ioctl als_data\n", lux_val);
    }

    if (lux_val<TAOS_ALS_GAIN_DIVIDE && gain_param!=TAOS_ALS_GAIN_8X)
    {
        taos_als_gain_set(TAOS_ALS_GAIN_8X);
    }
    else
    {
        if (lux_val>TAOS_ALS_GAIN_DIVIDE && gain_param!=TAOS_ALS_GAIN_1X)
        {
            taos_als_gain_set(TAOS_ALS_GAIN_1X);
        }
    }

    if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_ALS_TIME)))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte failed in ioctl als_data\n");
        return (ret);
    }

    reg_val = i2c_smbus_read_byte(taos_datap->client);

    if (taos_datap->als_debug)
    {
        SENSOR_LOG_ERROR(KERN_INFO "reg_val = %d lux_val = %d\n",reg_val,lux_val);
    }

    if (reg_val != prox_int_time_param)
    {
        lux_val = (lux_val * (101 - (0XFF - reg_val)))/20;
    }

    lux_val = taos_lux_filter(lux_val);

    if (taos_datap->als_debug)
    {
        SENSOR_LOG_ERROR(KERN_INFO "lux_val = %d",lux_val);
    }

    lux_val = lux_val * taos_datap->light_percent / 100;
    lux_val = lux_val > 10000 ? 10000 : lux_val;

    input_report_rel(taos_datap->a_idev, REL_X, lux_val+1);
    input_sync(taos_datap->a_idev);

    return ret;
}

static int taos_prox_threshold_set(void)//iVIZM
{
    int i,ret = 0;
    u8 chdata[6];
    u16 proxdata = 0;
    u16 cleardata = 0;

    for (i = 0; i < 6; i++) {
        chdata[i] = (i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CMD_WORD_BLK_RW| (TAOS_TRITON_ALS_CHAN0LO + i))));
    }
    cleardata = chdata[0] + chdata[1]*256;
    proxdata = chdata[4] + chdata[5]*256;
    last_proximity_data = proxdata;

	if (taos_datap->pro_ft || taos_datap->prox_debug)
    {
        pro_buf[0] = 0xff;
        pro_buf[1] = 0xff;
        pro_buf[2] = 0xff;
        pro_buf[3] = 0xff;

        for( mcount=0; mcount<4; mcount++ )
        {
            if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x08) + mcount, pro_buf[mcount]))) < 0)
            {
                 SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in taos prox threshold set\n");
                 return (ret);
            }
        }

        if (taos_datap->pro_ft)
        {
            SENSOR_LOG_INFO( "init the prox threshold\n");
        }

        if (taos_datap->prox_debug)
        {
            mdelay(prox_debug_delay_time);
            SENSOR_LOG_INFO( "proxdata = %d\n",proxdata);
	        input_report_rel(taos_datap->p_idev, REL_MISC, proxdata>0? proxdata:1);

        }
		taos_datap->pro_ft = false;
	}
    else
    {
        if (proxdata < taos_cfgp->prox_threshold_lo)
        {   //FAR
            pro_buf[0] = 0x0;
            pro_buf[1] = 0x0;
            pro_buf[2] = taos_cfgp->prox_threshold_hi & 0x0ff;
            pro_buf[3] = taos_cfgp->prox_threshold_hi >> 8;
    		SENSOR_LOG_INFO( "Far!!! proxdata = %d, hi = %d, low = %d\n",proxdata,taos_cfgp->prox_threshold_hi,taos_cfgp->prox_threshold_lo);
            input_report_rel(taos_datap->p_idev, REL_X, proxdata>0? proxdata:1);
        }
        else
        {
                if (proxdata > taos_cfgp->prox_threshold_hi)
                {   //NEAR
                    if (cleardata > ((sat_als*80)/100))
                    {
                        SENSOR_LOG_ERROR("%u <= %u*0.8 int data\n",proxdata,sat_als);
                        msleep(100);
                        return -ENODATA;
                    }
                    pro_buf[0] = taos_cfgp->prox_threshold_lo & 0x0ff;
                    pro_buf[1] = taos_cfgp->prox_threshold_lo >> 8;
                    pro_buf[2] = 0xff;
                    pro_buf[3] = 0xff;
                    SENSOR_LOG_INFO("Near!!! proxdata = %d, hi = %d, low = %d\n",proxdata,taos_cfgp->prox_threshold_hi,taos_cfgp->prox_threshold_lo);
                    input_report_rel(taos_datap->p_idev, REL_X, proxdata);
                }
                else
                {
                    if( (taos_cfgp->prox_threshold_hi-proxdata) > (proxdata-taos_cfgp->prox_threshold_lo))
                    {
                        //FAR
                        pro_buf[0] = 0x0;
                        pro_buf[1] = 0x0;
                        pro_buf[2] = taos_cfgp->prox_threshold_hi & 0x0ff;
                        pro_buf[3] = taos_cfgp->prox_threshold_hi >> 8;
                		SENSOR_LOG_INFO( "Far!!! proxdata = %d, hi = %d, low = %d\n",proxdata,taos_cfgp->prox_threshold_hi,taos_cfgp->prox_threshold_lo);
                        input_report_rel(taos_datap->p_idev, REL_X, (taos_cfgp->prox_threshold_lo-50)>0? (taos_cfgp->prox_threshold_lo-50):1);
                    }
                    else
                    {
                        //NEAR
                        if (cleardata > ((sat_als*80)/100))
                        {
                            SENSOR_LOG_ERROR("%u <= %u*0.8 int data\n",proxdata,sat_als);
                        	msleep(100);
                            return -ENODATA;
                        }
                        pro_buf[0] = taos_cfgp->prox_threshold_lo & 0x0ff;
                        pro_buf[1] = taos_cfgp->prox_threshold_lo >> 8;
                        pro_buf[2] = 0xff;
                        pro_buf[3] = 0xff;
                        SENSOR_LOG_INFO( "Near!!! proxdata = %d, hi = %d, low = %d\n",proxdata,taos_cfgp->prox_threshold_hi,taos_cfgp->prox_threshold_lo);
                        input_report_rel(taos_datap->p_idev, REL_X, taos_cfgp->prox_threshold_hi+50);
                    }
                }
            }
    }

    input_sync(taos_datap->p_idev);

    for( mcount=0; mcount<4; mcount++)
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x08) + mcount, pro_buf[mcount]))) < 0)
        {
             SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in taos prox threshold set\n");
             return (ret);
        }
    }

    return ret;
}

// driver init
static int __init taos_init(void)
{
#ifdef CONFIG_ZTEMT_SENSORS_ALS_PS_AUTO_DETECT
    return 0;
#else
    return i2c_add_driver(&tmd2772_driver);
#endif
}

// driver exit
static void __exit taos_exit(void)
{
	i2c_del_driver(&tmd2772_driver);
}

static int tmd2772_parse_dt(struct taos_data *chip)
{
    int rc = 0;
	u32 tmp;
	struct device_node *np = chip->client->dev.of_node;

	chip->irq_pin_num = of_get_named_gpio(np, "ams,irq-gpio", 0);
    SENSOR_LOG_INFO("irq_pin_num is %d\n",chip->irq_pin_num);

	rc = of_property_read_u32(np, "ams,prox-offset-cal-ability-tmd2772", &tmp);
	chip->prox_offset_cal_ability = (!rc ? tmp : 8);
    SENSOR_LOG_INFO("prox_offset_cal_ability is %d\n", chip->prox_offset_cal_ability);

	rc = of_property_read_u32(np, "ams,prox-led-plus-cnt-tmd2772", &tmp);
	chip->prox_led_plus_cnt = (!rc ? tmp : 8);
    SENSOR_LOG_INFO("prox_led_plus_cnt is %d\n", chip->prox_led_plus_cnt);

	rc = of_property_read_u32(np, "ams,light-percent", &tmp);
	chip->light_percent = (!rc ? tmp : 100);
    SENSOR_LOG_INFO("light_percent is %d\n", chip->light_percent);

    chip->prox_spy_enable = of_property_read_bool(np, "ams,prox-spy-enable");
    SENSOR_LOG_INFO("prox-spy is %s\n", chip->prox_spy_enable? "enable" : "disable");

    chip->prox_auto_calibrate = of_property_read_bool(np, "ams,prox-auto-calibrate");
    SENSOR_LOG_INFO("prox_auto_calibrate is %s\n", chip->prox_auto_calibrate? "enable" : "disable");

    return 0;
}

static void tmd2772_data_init(void)
{
    taos_datap->als_on  = false;
    taos_datap->prox_on = false;
    taos_datap->init = false;
    taos_datap->als_poll_time_mul = 1;
    taos_datap->prox_name = "proximity";
    taos_datap->als_name  = "light";
    taos_datap->chip_name = "tmd2772";
    taos_datap->prox_calibrate_result = false;
    taos_datap->prox_offset_cal_result = false;
	taos_datap->prox_offset_cal_verify = true;
	taos_datap->prox_calibrate_verify = true;
    taos_datap->prox_thres_hi_max = PROX_THRESHOLD_HIGH_MAX;
    taos_datap->prox_thres_hi_min = PROX_THRESHOLD_HIGH_MIN;
    taos_datap->prox_data_max     = PROX_DATA_MAX;
    taos_datap->prox_offset_cal_per_bit = taos_datap->prox_offset_cal_ability * taos_datap->prox_led_plus_cnt;
    taos_datap->prox_uncover_data = 0;
    taos_datap->prox_calibrate_times = 10;
    taos_datap->prox_thres_have_cal = false;
    taos_datap->prox_thres_type = PROX_THRESHOLD_USE_DEFAULT;
    taos_datap->proximity_wakelock.name = "proximity-wakelock";
    taos_datap->proximity_wakelock.locked = false;
    taos_datap->phone_is_sleep = false;
    taos_datap->irq_work_status = false;
    taos_datap->irq_enabled = true;
    taos_datap->prox_offset_cal_start = false;
    taos_datap->pro_ft = false;
    taos_datap->prox_debug = false;
    taos_datap->als_debug = false;
}

static int tmd2772_power_init(struct taos_data *chip, bool on)
{
	int rc = 0;

	SENSOR_LOG_ERROR("on = %d\n", on);

	if (!on) {
		if (regulator_count_voltages(chip->vdd) > 0)
			regulator_set_voltage(chip->vdd, 0,
				TMD2772_VDD_MAX_UV);
		regulator_put(chip->vdd);

		if (regulator_count_voltages(chip->vio) > 0)
			regulator_set_voltage(chip->vio, 0,
				TMD2772_VIO_MAX_UV);
		regulator_put(chip->vio);
	} else {
		chip->vdd = regulator_get(&chip->client->dev, "vdd");
		if (IS_ERR(chip->vdd)) {
			rc = PTR_ERR(chip->vdd);
			dev_err(&chip->client->dev,
				"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(chip->vdd) > 0) {
			rc = regulator_set_voltage(chip->vdd,
				TMD2772_VDD_MIN_UV, TMD2772_VDD_MAX_UV);
			if (rc) {
				dev_err(&chip->client->dev,
					"Regulator set failed vdd rc=%d\n",
					rc);
				goto err_vdd_set;
			}
		}

		chip->vio = regulator_get(&chip->client->dev, "vio");
		if (IS_ERR(chip->vio)) {
			rc = PTR_ERR(chip->vio);
			dev_err(&chip->client->dev,
				"Regulator get failed vio rc=%d\n", rc);
			goto err_vio_get;
		}

		if (regulator_count_voltages(chip->vio) > 0) {
			rc = regulator_set_voltage(chip->vio,
				TMD2772_VIO_MIN_UV, TMD2772_VIO_MAX_UV);
			if (rc) {
				dev_err(&chip->client->dev,
				"Regulator set failed vio rc=%d\n", rc);
				goto err_vio_set;
			}
		}

		rc = regulator_enable(chip->vdd);
		if (rc) {
			dev_err(&chip->client->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
			goto err_vdd_enable;
		}

		rc = regulator_enable(chip->vio);
		if (rc) {
			dev_err(&chip->client->dev,
				"Regulator vio enable failed rc=%d\n", rc);
			goto err_vio_enable;
		}
	}

	SENSOR_LOG_ERROR("success\n");

	return 0;

err_vio_enable:
	regulator_disable(chip->vdd);
err_vdd_enable:
	if (regulator_count_voltages(chip->vio) > 0)
		regulator_set_voltage(chip->vio, 0, TMD2772_VIO_MAX_UV);
err_vio_set:
	regulator_put(chip->vio);
err_vio_get:
	if (regulator_count_voltages(chip->vdd) > 0)
		regulator_set_voltage(chip->vdd, 0, TMD2772_VDD_MAX_UV);
err_vdd_set:
	regulator_put(chip->vdd);
	return rc;
}


static int tmd2772_chip_detect(struct taos_data *chip)
{
    int i                = 0;
    int ret              = 0;
    int chip_id          = 0;
    int detect_max_times = 10;

    for (i=0; i<detect_max_times; i++)
    {
        chip_id = i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | (TAOS_TRITON_CNTRL + TAOS_TRITON_CHIPID)));

        if (chip_id == 0x39)
        {
            break;
        }
        else
        {
            SENSOR_LOG_ERROR("retry %d time, chip id is [0x%x] was read does not match [TMD27723-0x39]\n", i+1, chip_id);
        }
    }

    if (i>=detect_max_times)
    {
        SENSOR_LOG_ERROR("chip detect failed\n");
        ret =  -ENODEV;
    }

    return ret;
}

// client probe
static int tmd2772_probe(struct i2c_client *clientp, const struct i2c_device_id *idp)
{
    int ret = 0;
    int i = 0;
    unsigned char buf[TAOS_MAX_DEVICE_REGS];
    char *device_name;
    SENSOR_LOG_INFO("Prob Start\n");


    if (!i2c_check_functionality(clientp->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
    {
        SENSOR_LOG_ERROR("i2c smbus byte data functions unsupported\n");
        return -EOPNOTSUPP;
    }

    taos_datap = kmalloc(sizeof(struct taos_data), GFP_KERNEL);
    if (!taos_datap)
    {
         SENSOR_LOG_ERROR("kmalloc for struct taos_data failed\n");
         return -ENOMEM;
    }

    taos_datap->client = clientp;

    i2c_set_clientdata(clientp, taos_datap);

    ret = tmd2772_power_init(taos_datap, 1);
	if (ret < 0)
    {
		goto power_init_failed;
    }

    msleep(10);

    ret = tmd2772_chip_detect(taos_datap);
    if (ret)
    {
        goto read_chip_id_failed;
    }

    INIT_WORK(&(taos_datap->irq_work),taos_irq_work_func);

	sema_init(&taos_datap->update_lock,1);
	mutex_init(&(taos_datap->lock));
	mutex_init(&(taos_datap->prox_work_lock));
    wake_lock_init(&taos_datap->proximity_wakelock.lock, WAKE_LOCK_SUSPEND, "proximity-wakelock");

    tmd2772_parse_dt(taos_datap);

    tmd2772_data_init();

    for (i = 0; i < TAOS_MAX_DEVICE_REGS; i++)
    {
        if ((ret = (i2c_smbus_write_byte(clientp, (TAOS_TRITON_CMD_REG | (TAOS_TRITON_CNTRL + i))))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte() to control reg failed in taos_probe()\n");
            return(ret);
        }
        buf[i] = i2c_smbus_read_byte(clientp);
    }

    if ((ret = taos_device_name(buf, &device_name)) == 0)
    {
        SENSOR_LOG_ERROR("chip id that was read found mismatched by taos_device_name(), in taos_probe()\n");
        return -ENODEV;
    }
    if (strcmp(device_name, TAOS_DEVICE_ID))
    {
        SENSOR_LOG_ERROR("chip id that was read does not match expected id in taos_probe()\n");
        return -ENODEV;
    }
    else
    {
        SENSOR_LOG_ERROR("chip id of %s that was read matches expected id in taos_probe()\n", device_name);
    }
    if ((ret = (i2c_smbus_write_byte(clientp, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL)))) < 0) {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte() to control reg failed in taos_probe()\n");
        return(ret);
    }
    strlcpy(clientp->name, TAOS_DEVICE_ID, I2C_NAME_SIZE);
    strlcpy(taos_datap->taos_name, TAOS_DEVICE_ID, TAOS_ID_NAME_SIZE);

    taos_datap->valid = 0;
    if (!(taos_cfgp = kmalloc(sizeof(struct taos_cfg), GFP_KERNEL))) {
        SENSOR_LOG_ERROR("kmalloc for struct taos_cfg failed in taos_probe()\n");
        return -ENOMEM;
    }
    taos_cfgp->als_time = als_time_param;
    taos_cfgp->scale_factor_als = scale_factor_param_als;
	taos_cfgp->scale_factor_prox = scale_factor_param_prox;
    taos_cfgp->gain_trim = gain_trim_param;
    taos_cfgp->filter_history = filter_history_param;
    taos_cfgp->filter_count = filter_count_param;
    taos_cfgp->gain = gain_param;
    taos_cfgp->als_threshold_hi = als_threshold_hi_param;//iVIZM
    taos_cfgp->als_threshold_lo = als_threshold_lo_param;//iVIZM
    taos_cfgp->prox_threshold_hi = prox_threshold_hi_param;
    taos_cfgp->prox_threshold_lo = prox_threshold_lo_param;
    taos_cfgp->prox_int_time = prox_int_time_param;
    taos_cfgp->prox_adc_time = prox_adc_time_param;
    taos_cfgp->prox_wait_time = prox_wait_time_param;
    taos_cfgp->prox_intr_filter = prox_intr_filter_param;
    taos_cfgp->prox_config = prox_config_param;
    taos_cfgp->prox_pulse_cnt = prox_pulse_cnt_param;
    taos_cfgp->prox_gain = prox_gain_param;
    taos_cfgp->prox_config_offset=prox_config_offset_param;
    sat_als = (256 - taos_cfgp->prox_int_time) << 10;
    sat_prox = (256 - taos_cfgp->prox_adc_time) << 10;

    /*dmobile ::power down for init ,Rambo liu*/
    SENSOR_LOG_ERROR("light sensor will pwr down \n");
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x00), 0x00))) < 0) {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in power down\n");
        return (ret);
    }

    taos_datap->irq_work_queue = create_singlethread_workqueue("taos_work_queue");
    if (!taos_datap->irq_work_queue)
    {
        ret = -ENOMEM;
        SENSOR_LOG_INFO( "cannot create work taos_work_queue, err = %d",ret);
        return ret;
    }

    ret = gpio_request(taos_datap->irq_pin_num, "ALS_PS_INT");
    if (ret)
    {
        SENSOR_LOG_INFO("gpio %d is busy and then to free it\n",taos_datap->irq_pin_num);

        gpio_free(taos_datap->irq_pin_num);
        ret = gpio_request(taos_datap->irq_pin_num, "ALS_PS_INT");
        if (ret)
        {
            SENSOR_LOG_INFO("gpio %d is busy and then to free it\n",taos_datap->irq_pin_num);
            return ret;
        }
    }
    else
    {
        SENSOR_LOG_INFO("get gpio %d success\n",taos_datap->irq_pin_num);
    }

    gpio_direction_input(taos_datap->irq_pin_num);
    SENSOR_LOG_INFO("gpio is %d \n",gpio_get_value(taos_datap->irq_pin_num));

    taos_datap->client->irq = gpio_to_irq(taos_datap->irq_pin_num);

	ret = request_threaded_irq(taos_datap->client->irq, NULL, &taos_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "taos_irq", taos_datap);
    if (ret != 0)
    {
        gpio_free(taos_datap->irq_pin_num);
        return(ret);
    }

    taos_irq_ops(false, true);
    INIT_DELAYED_WORK(&taos_datap->als_poll_work, taos_als_poll_work_func);
    INIT_DELAYED_WORK(&taos_datap->prox_calibrate_work, taos_prox_calibrate_work_func);
    INIT_DELAYED_WORK(&taos_datap->prox_offset_cal_work, taos_prox_offset_cal_work_func);
    INIT_DELAYED_WORK(&taos_datap->prox_flush_work, taos_flush_work_func);


    hrtimer_init(&taos_datap->prox_unwakelock_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    (taos_datap->prox_unwakelock_timer).function = taos_prox_unwakelock_work_func ;
    proximity_class = class_create(THIS_MODULE, "proximity");
    light_class     = class_create(THIS_MODULE, "light");

    taos_datap->proximity_dev = device_create(proximity_class, NULL, tmd2772_proximity_dev_t, &tmd2772_driver ,"proximity");
    if (IS_ERR(taos_datap->proximity_dev))
    {
      ret = PTR_ERR(taos_datap->proximity_dev);
      SENSOR_LOG_ERROR("device_create proximity failed\n");
      goto create_proximity_dev_failed;
    }

    taos_datap->light_dev= device_create(light_class, NULL, tmd2772_light_dev_t, &tmd2772_driver ,"light");
    if (IS_ERR(taos_datap->light_dev))
    {
      ret = PTR_ERR(taos_datap->light_dev);
      SENSOR_LOG_ERROR("device_create light failed\n");
      goto create_light_dev_failed;
    }

    //prox input
    taos_datap->p_idev = input_allocate_device();
    if (!taos_datap->p_idev)
    {
        SENSOR_LOG_ERROR("no memory for input_dev '%s'\n",taos_datap->prox_name);
        ret = -ENODEV;
        goto input_p_alloc_failed;
    }
    taos_datap->p_idev->name = taos_datap->prox_name;
    taos_datap->p_idev->id.bustype = BUS_I2C;
    dev_set_drvdata(&taos_datap->p_idev->dev, taos_datap);
    ret = input_register_device(taos_datap->p_idev);
    if (ret)
    {
        input_free_device(taos_datap->p_idev);
        SENSOR_LOG_ERROR("cant register input '%s'\n",taos_datap->prox_name);
        goto input_p_register_failed;
    }

    set_bit(EV_REL, taos_datap->p_idev->evbit);
    set_bit(REL_X,  taos_datap->p_idev->relbit);
    set_bit(REL_Y,  taos_datap->p_idev->relbit);
    set_bit(REL_Z,  taos_datap->p_idev->relbit);
    set_bit(REL_MISC,  taos_datap->p_idev->relbit);

    //light input
    taos_datap->a_idev = input_allocate_device();
	if (!taos_datap->a_idev)
    {
		SENSOR_LOG_ERROR("no memory for input_dev '%s'\n",taos_datap->als_name);
		ret = -ENODEV;
		goto input_a_alloc_failed;
	}
	taos_datap->a_idev->name = taos_datap->als_name;
	taos_datap->a_idev->id.bustype = BUS_I2C;

    set_bit(EV_REL, taos_datap->a_idev->evbit);
    set_bit(REL_X,  taos_datap->a_idev->relbit);
    set_bit(REL_Y,  taos_datap->a_idev->relbit);

	dev_set_drvdata(&taos_datap->a_idev->dev, taos_datap);
	ret = input_register_device(taos_datap->a_idev);
	if (ret)
    {
		input_free_device(taos_datap->a_idev);
		SENSOR_LOG_ERROR("cant register input '%s'\n",taos_datap->prox_name);
		goto input_a_register_failed;
	}

	dev_set_drvdata(taos_datap->proximity_dev, taos_datap);
	dev_set_drvdata(taos_datap->light_dev, taos_datap);


    create_sysfs_interfaces_prox(taos_datap->proximity_dev);
    create_sysfs_interfaces_light(taos_datap->light_dev);

    SENSOR_LOG_INFO("Prob OK\n");

	return 0;

input_a_register_failed:
    input_free_device(taos_datap->a_idev);
input_a_alloc_failed:
input_p_register_failed:
    input_free_device(taos_datap->p_idev);
input_p_alloc_failed:
create_light_dev_failed:
    taos_datap->light_dev = NULL;
    class_destroy(light_class);
create_proximity_dev_failed:
    taos_datap->proximity_dev = NULL;
    class_destroy(proximity_class);
read_chip_id_failed:
	tmd2772_power_init(taos_datap, 0);
power_init_failed:
    kfree(taos_datap);

    SENSOR_LOG_INFO("Prob Failed\n");

    return (ret);
}

#ifdef CONFIG_PM_SLEEP
//resume
static int taos_resume(struct i2c_client *client) {
	int ret = 0;
    SENSOR_LOG_INFO("enter\n");
	if(1 == taos_datap->prox_on)
    {
        SENSOR_LOG_INFO( "disable irq wakeup\n");
		ret = disable_irq_wake(taos_datap->client->irq);
	}
    if(ret < 0)
		SENSOR_LOG_ERROR("disable_irq_wake failed\n");
    SENSOR_LOG_INFO("eixt\n");
    return ret ;
}

//suspend
static int taos_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret = 0;
    SENSOR_LOG_INFO("enter\n");
	if(1 == taos_datap->prox_on)
    {
        SENSOR_LOG_INFO( "enable irq wakeup\n");
       	ret = enable_irq_wake(taos_datap->client->irq);
    }
	if(ret < 0)
    {
		SENSOR_LOG_ERROR("enable_irq_wake failed\n");
    }

    wakeup_from_sleep = true;

    SENSOR_LOG_INFO("eixt\n");
    return ret ;
}

#endif
// client remove
static int tmd2772_remove(struct i2c_client *client) {
    int ret = 0;

    return (ret);
}


// read/calculate lux value
static int taos_get_lux(void)
{
    int raw_clear = 0, raw_ir = 0, raw_lux = 0;
    u32 lux = 0;
    u32 ratio = 0;
    u8 dev_gain = 0;
    u16 Tint = 0;
    struct lux_data *p;
    int ret = 0;
    u8 chdata[4];
    int tmp = 0, i = 0,tmp_gain=1;
    for (i = 0; i < 4; i++) {
        if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | (TAOS_TRITON_ALS_CHAN0LO + i))))) < 0) {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte() to chan0/1/lo/hi reg failed in taos_get_lux()\n");
            return (ret);
        }
        chdata[i] = i2c_smbus_read_byte(taos_datap->client);
    }

    tmp = (taos_cfgp->als_time + 25)/50;            //if atime =100  tmp = (atime+25)/50=2.5   time = 2.7*(256-atime)=  412.5
    TritonTime.numerator = 1;
    TritonTime.denominator = tmp;

    tmp = 300 * taos_cfgp->als_time;               //tmp = 300*atime  400
    if(tmp > 65535)
        tmp = 65535;
    TritonTime.saturation = tmp;
    raw_clear = chdata[1];
    raw_clear <<= 8;
    raw_clear |= chdata[0];
    raw_ir    = chdata[3];
    raw_ir    <<= 8;
    raw_ir    |= chdata[2];

    raw_clear *= ((taos_cfgp->scale_factor_als )*tmp_gain);
    raw_ir *= (taos_cfgp->scale_factor_prox );

    if(raw_ir > raw_clear) {
        raw_lux = raw_ir;
        raw_ir = raw_clear;
        raw_clear = raw_lux;
    }
    dev_gain = taos_triton_gain_table[taos_cfgp->gain & 0x3];
    if(raw_clear >= lux_timep->saturation)
        return(TAOS_MAX_LUX);
    if(raw_ir >= lux_timep->saturation)
        return(TAOS_MAX_LUX);
    if(raw_clear == 0)
        return(0);
    if(dev_gain == 0 || dev_gain > 127) {
        SENSOR_LOG_ERROR("dev_gain = 0 or > 127 in taos_get_lux()\n");
        return -1;
    }
    if(lux_timep->denominator == 0) {
        SENSOR_LOG_ERROR("lux_timep->denominator = 0 in taos_get_lux()\n");
        return -1;
    }
    ratio = (raw_ir<<15)/raw_clear;
    for (p = lux_tablep; p->ratio && p->ratio < ratio; p++);
	#ifdef WORK_UES_POLL_MODE
    if(!p->ratio) {//iVIZM
        if(lux_history[0] < 0)
            return 0;
        else
            return lux_history[0];
    }
	#endif
    Tint = taos_cfgp->als_time;
    raw_clear = ((raw_clear*400 + (dev_gain>>1))/dev_gain + (Tint>>1))/Tint;
    raw_ir = ((raw_ir*400 +(dev_gain>>1))/dev_gain + (Tint>>1))/Tint;
    lux = ((raw_clear*(p->clear)) - (raw_ir*(p->ir)));
    lux = (lux + 32000)/64000;
    if(lux > TAOS_MAX_LUX) {
        lux = TAOS_MAX_LUX;
    }
    return(lux);
}

static int taos_lux_filter(int lux)
{
    static u8 middle[] = {1,0,2,0,0,2,0,1};
    int index;

    lux_history[2] = lux_history[1];
    lux_history[1] = lux_history[0];
    lux_history[0] = lux;

    if(lux_history[2] < 0) { //iVIZM
        if(lux_history[1] > 0)
            return lux_history[1];
        else
            return lux_history[0];
    }
    index = 0;
    if( lux_history[0] > lux_history[1] )
        index += 4;
    if( lux_history[1] > lux_history[2] )
        index += 2;
    if( lux_history[0] > lux_history[2] )
        index++;
    return(lux_history[middle[index]]);
}

// verify device
static int taos_device_name(unsigned char *bufp, char **device_name)
{
    *device_name="tritonFN";
    return(1);
}

// proximity poll
static int taos_prox_poll(struct taos_prox_info *prxp)
{
    int i = 0, ret = 0; //wait_count = 0;
    u8 chdata[6];

    for (i = 0; i < 6; i++) {
        chdata[i] = (i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CMD_AUTO | (TAOS_TRITON_ALS_CHAN0LO + i))));
    }
    prxp->prox_clear = chdata[1];
    prxp->prox_clear <<= 8;
    prxp->prox_clear |= chdata[0];
    if (prxp->prox_clear > ((sat_als*80)/100))
    {
		SENSOR_LOG_ERROR("%u <= %u*0.8 poll data\n",prxp->prox_clear,sat_als);
        return -ENODATA;
    }
    prxp->prox_data = chdata[5];
    prxp->prox_data <<= 8;
    prxp->prox_data |= chdata[4];

    return (ret);
}

// prox poll timer function
static void taos_prox_poll_timer_func(unsigned long param) {
    int ret = 0;

    if (!device_released) {
        if ((ret = taos_prox_poll(prox_cur_infop)) < 0) {
            SENSOR_LOG_ERROR("call to prox_poll failed in taos_prox_poll_timer_func()\n");
            return;
        }
        taos_prox_poll_timer_start();
    }
    return;
}

// start prox poll timer
static void taos_prox_poll_timer_start(void) {
    init_timer(&prox_poll_timer);
    prox_poll_timer.expires = jiffies + (HZ/10);
    prox_poll_timer.function = taos_prox_poll_timer_func;
    add_timer(&prox_poll_timer);
    return;
}

static void taos_update_sat_als(void)
{
    u8 reg_val = 0;
    int ret = 0;

    if ((ret = (i2c_smbus_write_byte(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_ALS_TIME)))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte failed in ioctl als_calibrate\n");
        return;
    }

    reg_val = i2c_smbus_read_byte(taos_datap->client);

    sat_als = (256 - reg_val) << 10;
}

static int taos_als_gain_set(unsigned als_gain)
{
    int ret;
    prox_gain_param = (prox_gain_param & 0xFC) | als_gain;
    gain_param      = prox_gain_param & 0x03;

    if (NULL!=taos_cfgp)
    {
        taos_cfgp->gain      = gain_param;
        taos_cfgp->prox_gain = prox_gain_param;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the prox_led_strength reg\n");
            return -EINVAL;
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
        return -EINVAL;
    }

    return ret;
}

static void taos_als_poll_work_func(struct work_struct *work)
{
    taos_als_get_data();
    if (true == taos_datap->als_on)
    {
        schedule_delayed_work(&taos_datap->als_poll_work, msecs_to_jiffies(als_poll_time_mul*als_poll_delay));
    }
}

static void taos_prox_calibrate_work_func(struct work_struct *work)
{
	taos_prox_calibrate();
}

static int taos_prox_offset_cal_prepare(void)
{
    int ret =1;
    if (NULL!=taos_cfgp)
    {
        taos_cfgp->prox_config_offset = 0;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0)
        {
            SENSOR_LOG_ERROR("failed to write the prox_config_offset  reg\n");
	     return ret;
        }
    }
    else
    {
        SENSOR_LOG_ERROR("taos_cfgp is NULL\n");
	 return -EINVAL;
    }
	 return ret;
}

static int taos_prox_offset_calculate(int data, int target)
{
    int offset;

    if (data > PROX_DATA_TARGET)
    {
        offset = (data - PROX_DATA_TARGET) * 10 / taos_datap->prox_offset_cal_per_bit;
    }
    else
    {
        offset = (PROX_DATA_TARGET - data) * 16 / taos_datap->prox_offset_cal_per_bit + 128;
    }

   SENSOR_LOG_ERROR("------------ taos_cfgp->prox_offset = %d\n",offset );

    return offset;
}

static int taos_prox_uncover_data_get(void)
{
    u8 i = 0, j = 0;
    int prox_sum = 0, ret = 0;
    static struct taos_prox_info prox_info_temp;

    for (i = 0, j = 0; i < PROX_OFFSET_CAL_BUFFER_SIZE / 5; i++)
    {
        if ((ret = taos_prox_poll(&prox_info_temp)) < 0)
        {
            SENSOR_LOG_ERROR("failed to tmd2772_prox_read_data\n");
        }
        else
        {
            j++;
            prox_sum += prox_info_temp.prox_data;
            SENSOR_LOG_ERROR("prox_data is %d\n",prox_info_temp.prox_data);
        }
        mdelay(20);
    }

    if(j == 0)
    {
        ret = -1;
        goto error;
    }

    taos_datap->prox_uncover_data = prox_sum / j;
    SENSOR_LOG_ERROR("prox_uncover_data = %d\n", taos_datap->prox_uncover_data);

    if (taos_datap->prox_uncover_data < PROX_DATA_SAFE_RANGE_MAX)
    {
        taos_datap->prox_thres_hi_min = taos_datap->prox_uncover_data + PROX_THRESHOLD_SAFE_DISTANCE;
        taos_datap->prox_thres_hi_max = taos_datap->prox_thres_hi_min + PROX_THRESHOLD_DISTANCE / 2;
        taos_datap->prox_thres_hi_max = (taos_datap->prox_thres_hi_max > PROX_THRESHOLD_HIGH_MAX) ? PROX_THRESHOLD_HIGH_MAX : taos_datap->prox_thres_hi_max;
        taos_datap->prox_thres_lo_min = taos_datap->prox_uncover_data + PROX_THRESHOLD_DISTANCE;
        taos_datap->prox_thres_lo_max = taos_datap->prox_uncover_data + PROX_THRESHOLD_DISTANCE * 2;
        SENSOR_LOG_ERROR("get uncover data success\n");
    }
    else
    {
        taos_datap->prox_thres_hi_min = PROX_DEFAULT_THRESHOLD_HIGH;
        taos_datap->prox_thres_hi_max = PROX_DEFAULT_THRESHOLD_HIGH;
        taos_datap->prox_thres_lo_min = PROX_DEFAULT_THRESHOLD_LOW;
        taos_datap->prox_thres_lo_max = PROX_DEFAULT_THRESHOLD_LOW;
        SENSOR_LOG_ERROR("get uncover data failed for data too high\n");
    }

    SENSOR_LOG_ERROR("prox_thres_hi range is [%d--%d]\n", taos_datap->prox_thres_hi_min, taos_datap->prox_thres_hi_max);
    SENSOR_LOG_ERROR("prox_thres_lo range is [%d--%d]\n", taos_datap->prox_thres_lo_min, taos_datap->prox_thres_lo_max);

    taos_write_cal_file(PATH_PROX_UNCOVER_DATA, taos_datap->prox_uncover_data);

    return 0;

error:
    return ret;
}

static int taos_prox_offset_cal_process(u32 prox_offset_cal_buffer_size)
{
    int ret;
    int prox_sum = 0, prox_mean = 0;
    int i = 0, j = 0;
    u8 reg_val = 0;
    u8 reg_cntrl = 0;

    struct taos_prox_info *prox_cal_info = NULL;
        prox_cal_info = kmalloc(sizeof(struct taos_prox_info) * (prox_offset_cal_buffer_size), GFP_KERNEL);
        if (NULL == prox_cal_info)
        {
            SENSOR_LOG_ERROR("malloc prox_cal_info failed\n");
            ret = -1;
            goto prox_offset_cal_buffer_error;
        }
        memset(prox_cal_info, 0, sizeof(struct taos_prox_info) * (taos_datap->prox_calibrate_times));

        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x01), taos_cfgp->prox_int_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_int_time reg\n");
            goto prox_calibrate_offset_error;
        }
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x02), taos_cfgp->prox_adc_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_adc_time reg\n");
            goto prox_calibrate_offset_error;
        }
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x03), taos_cfgp->prox_wait_time))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_wait_time reg\n");
            goto prox_calibrate_offset_error;
        }

        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0D), taos_cfgp->prox_config))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_config reg\n");
            goto prox_calibrate_offset_error;
        }

        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0E), taos_cfgp->prox_pulse_cnt))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_pulse_cnt reg\n");
            goto prox_calibrate_offset_error;
        }
	 if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0) {
        SENSOR_LOG_ERROR(KERN_ERR "i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        return (ret);
    }
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
        {
            SENSOR_LOG_ERROR("failed write prox_gain reg\n");
            goto prox_calibrate_offset_error;
        }

        reg_cntrl = reg_val | (TAOS_TRITON_CNTL_PROX_DET_ENBL | TAOS_TRITON_CNTL_PWRON | TAOS_TRITON_CNTL_ADC_ENBL);
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), reg_cntrl))) < 0)
        {
           SENSOR_LOG_ERROR("failed write cntrl reg\n");
           goto prox_calibrate_offset_error;
        }

        mdelay(30);

        for (i = 0; i < (prox_offset_cal_buffer_size); i++)
        {
            if ((ret = taos_prox_poll(&prox_cal_info[i])) < 0)
            {
            	   j++;
                SENSOR_LOG_ERROR("call to prox_poll failed in ioctl prox_calibrate\n");
            }
            prox_sum += prox_cal_info[i].prox_data;

            SENSOR_LOG_ERROR("prox get time %d data is %d\n",i,prox_cal_info[i].prox_data);
            mdelay(30);
        }

        prox_mean = prox_sum/(prox_offset_cal_buffer_size);
        SENSOR_LOG_ERROR("taos_cfgp->prox_mean = %d\n",prox_mean );
	    if(j != 0)
	    {
            goto prox_calibrate_offset_error;
        }

	    prox_config_offset_param = taos_prox_offset_calculate(prox_mean, PROX_DATA_TARGET);

        taos_cfgp->prox_config_offset = prox_config_offset_param;

        if((ret=taos_write_cal_file(PATH_PROX_OFFSET,taos_cfgp->prox_config_offset)) < 0)
	    {
	        goto prox_calibrate_offset_error;
        }

        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0)
        {
            SENSOR_LOG_ERROR(KERN_ERR "i2c_smbus_write_byte_data failed in ioctl prox_on\n");
            return (ret);
        }

        msleep(100);

        //read prox data register after update offset register
        ret = taos_prox_uncover_data_get();
        if (ret < 0)
        {
            SENSOR_LOG_ERROR("failed to tmd2772_prox_uncover_data_get\n");
            goto prox_calibrate_offset_error;
        }
        else
        {
            if (taos_datap->prox_thres_have_cal == true)
            {
                taos_prox_threshold_safe_check();
            }
        }

        for (i = 0; i < sizeof(taos_triton_reg_init); i++)
        {
            if(i !=11)
            {
                if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|(TAOS_TRITON_CNTRL +i)), taos_triton_reg_init[i]))) < 0)
                {
                    SENSOR_LOG_ERROR("failed write triton_init reg\n");
                               goto prox_calibrate_offset_error;

                }
             }
         }




	 kfree(prox_cal_info);
	return 1;
prox_calibrate_offset_error:
    SENSOR_LOG_ERROR("ERROR\n");
	 kfree(prox_cal_info);
prox_offset_cal_buffer_error:

	return -1;
    }

static void taos_prox_offset_cal_finish(void)
{
    taos_datap->prox_offset_cal_start = false;
    if (true == (taos_datap->prox_on))
    {
        taos_prox_on();
    }
    else
    {
        taos_prox_off();
    }
}
static int taos_prox_offset_cal(void)
{
    int ret = 0;
    taos_datap->prox_offset_cal_result = false;

    if ((ret=taos_prox_offset_cal_prepare())<0)
	goto error;

    mdelay(50);

    if ((ret= taos_prox_offset_cal_process(PROX_OFFSET_CAL_BUFFER_SIZE))>=0)
    {
        taos_datap->prox_offset_cal_result = true;
    }

    taos_prox_offset_cal_finish();

    return ret;
error:
    return ret;
}


static void taos_prox_offset_cal_work_func(struct work_struct *work)
{
	taos_prox_offset_cal();
}
static enum hrtimer_restart  taos_prox_unwakelock_work_func(struct hrtimer *timer)
{
	SENSOR_LOG_INFO("######## taos_prox_unwakelock_timer_func #########\n");
	if(false == taos_datap->irq_work_status )
	taos_wakelock_ops(&(taos_datap->proximity_wakelock),false);
	return HRTIMER_NORESTART;

}

static int taos_sensors_als_poll_on(void)
{
    int  ret = 0, i = 0;
    u8   reg_val = 0, reg_cntrl = 0;

    SENSOR_LOG_INFO("######## ALS ON #########\n");

    for (i = 0; i < TAOS_FILTER_DEPTH; i++)
    {
        lux_history[i] = -ENODATA;
    }

    if (taos_datap->prox_on)
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_ALS_TIME), TAOS_ALS_ADC_TIME_WHEN_PROX_ON))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
            return (ret);
        }
    }
    else
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_ALS_TIME), taos_cfgp->prox_int_time))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl als_on\n");
            return (ret);
        }
    }

    reg_val = i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_GAIN));

    //SENSOR_LOG_INFO("reg[0x0F] = 0x%02X\n",reg_val);

    reg_val = reg_val & 0xFC;
    reg_val = reg_val | (taos_cfgp->gain & 0x03);//*16
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_GAIN), reg_val))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl als_on\n");
        return (ret);
    }

    reg_cntrl = i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL));
    SENSOR_LOG_INFO("reg[0x00] = 0x%02X\n",reg_cntrl);

    reg_cntrl |= (TAOS_TRITON_CNTL_ADC_ENBL | TAOS_TRITON_CNTL_PWRON);
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), reg_cntrl))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl als_on\n");
        return (ret);
    }

	schedule_delayed_work(&taos_datap->als_poll_work, msecs_to_jiffies(200));

    taos_datap->als_on = true;

    taos_update_sat_als();

	return ret;
}

static int taos_sensors_als_poll_off(void)
{
    int  ret = 0, i = 0;
    u8  reg_val = 0;

    SENSOR_LOG_INFO("######## ALS OFF #########\n");

    for (i = 0; i < TAOS_FILTER_DEPTH; i++)
    {
        lux_history[i] = -ENODATA;
    }

    reg_val = i2c_smbus_read_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL));

    if ((reg_val & TAOS_TRITON_CNTL_PROX_DET_ENBL) == 0x00 && (0 == taos_datap->prox_on))
    {
        SENSOR_LOG_INFO("TAOS_TRITON_CNTL_PROX_DET_ENBL = 0\n");
        reg_val = 0x00;
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), reg_val))) < 0)
        {
           SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl als_off\n");
           return (ret);
        }
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|TAOS_TRITON_ALS_TIME), 0XFF))) < 0)
    {
       SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
       return (ret);
    }

    taos_datap->als_on = false;

    cancel_delayed_work_sync(&taos_datap->als_poll_work);

    taos_update_sat_als();

    return (ret);
}

static int taos_get_light_data(struct taos_data *p_taos_data)
{
    int raw_clear   = 0;
    int raw_ir      = 0;
    int ret         = 0;
    u8  i           = 0;
    u8  chdata[4];

    for (i = 0; i < 4; i++)
    {
        if ((ret = (i2c_smbus_write_byte(p_taos_data->client, (TAOS_TRITON_CMD_REG | (TAOS_TRITON_ALS_CHAN0LO + i))))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte() to chan0/1/lo/hi reg failed in taos_get_lux()\n");
            return (ret);
        }
        chdata[i] = i2c_smbus_read_byte(p_taos_data->client);
    }

    raw_clear = chdata[1];
    raw_clear <<= 8;
    raw_clear |= chdata[0];
    raw_ir    = chdata[3];
    raw_ir    <<= 8;
    raw_ir    |= chdata[2];

    //SENSOR_LOG_INFO("raw_clear = %d, raw_ir = %d\n",raw_clear, raw_ir);

    return (raw_clear > raw_ir ? raw_clear : raw_ir);
}

static void taos_prox_offset_need_cal_check(struct taos_data *p_taos_data)
{
    struct taos_prox_info prox_info;
    int i = 0;
    int j = 0;
    int prox_data = 0;
    int ret = 0;

    for (i = 0; i < 5; i++)
    {
        msleep(20);

        if ((ret = taos_prox_poll(&prox_info)) < 0)
        {
            SENSOR_LOG_ERROR("call to prox_poll failed in ioctl prox_calibrate\n");
        }
        else
        {
            prox_data = prox_data + prox_info.prox_data;
            j++;
        }
    }

    prox_data = prox_data / j;

    SENSOR_LOG_INFO("prox_data is %d, prox_uncover_data = %d\n",prox_data, p_taos_data->prox_uncover_data);

    if ((p_taos_data->prox_uncover_data - prox_data > PROX_NEED_OFFSET_CAL_THRESHOLD) ||
        (prox_data - p_taos_data->prox_uncover_data > PROX_NEED_OFFSET_CAL_THRESHOLD))
    {

        p_taos_data->prox_offset_need_cal = true;
    }
    else
    {
        p_taos_data->prox_offset_need_cal = false;
    }

    SENSOR_LOG_INFO("prox offset %s calibrate\n", true==p_taos_data->prox_offset_need_cal ? "need" : "no need");
}

static void taos_uncover_check(struct taos_data *p_taos_data)
{
    u32 lux_data    = 0;
    u32 chanle_data = 0;

    if (false==p_taos_data->als_on)
    {
        taos_sensors_als_poll_on();
    }

    chanle_data = taos_get_light_data(p_taos_data);

    lux_data = taos_get_lux();
    SENSOR_LOG_INFO("chanle_data is %d, lux_data is %d\n",chanle_data, lux_data);

    if (false==p_taos_data->als_on)
    {
        taos_sensors_als_poll_off();
    }

    if (chanle_data>PROX_LIGHT_CHANEL_UNCOVERY_THRES ||
        lux_data>PROX_LIGHT_LUX_UNCOVERY_THRES)
    {
        p_taos_data->prox_uncover = true;
    }
    else
    {
        p_taos_data->prox_uncover = false;
    }

    SENSOR_LOG_INFO("the phone is %s\n", true==p_taos_data->prox_uncover ? "no coverred" : "coverred");
}

static void taos_prox_spy(struct taos_data *p_taos_data)
{
    SENSOR_LOG_INFO("enter\n");

    taos_uncover_check(p_taos_data);
    taos_prox_offset_need_cal_check(p_taos_data);

    if (p_taos_data->prox_uncover)
    {
        if (p_taos_data->prox_offset_need_cal)
        {
            p_taos_data->prox_offset_cal_start = true;
            schedule_delayed_work(&p_taos_data->prox_offset_cal_work, msecs_to_jiffies(0));
        }
        else
        {
            taos_prox_threshold_use_calibrate(p_taos_data);
        }
    }
    else
    {
        if (p_taos_data->prox_offset_need_cal)
        {
            taos_prox_threshold_use_default(taos_cfgp);
        }
        else
        {
            taos_prox_threshold_use_calibrate(p_taos_data);
        }
    }

    SENSOR_LOG_INFO("exit\n");
}

static void taos_prox_threshold_safe_check(void)
{
    taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi < taos_datap->prox_thres_hi_max) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_max;
    taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > taos_datap->prox_thres_hi_min) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_min;

    taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;

    taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo < taos_datap->prox_thres_lo_max) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_max;
    taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo > taos_datap->prox_thres_lo_min) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_min;

    SENSOR_LOG_ERROR("prox_th_high  = %d\n",taos_cfgp->prox_threshold_hi);
    SENSOR_LOG_ERROR("prox_th_low   = %d\n",taos_cfgp->prox_threshold_lo);

    input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
    input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
    input_sync(taos_datap->p_idev);
}

static int taos_prox_threshold_use_calibrate(struct taos_data *p_taos_data)
{
	int ret =0;
	int err =0;

    if (p_taos_data->prox_thres_type == PROX_THRESHOLD_USE_CALIBRATE)
	{
	    SENSOR_LOG_INFO("prox threshold already use calibrate, no need to read calibrate\n");
	    goto taos_prox_threshold_use_calibrate_finish;
	}
	else
	{
        p_taos_data->prox_thres_type = PROX_THRESHOLD_USE_CALIBRATE;
	    SENSOR_LOG_INFO("prox threshold now use default, try to read calibrate\n");
	}

    if( (ret=taos_read_cal_value(CAL_THRESHOLD))<0)
    {
	    SENSOR_LOG_INFO("read threshold failed\n");
    }
	else
	{
		taos_cfgp->prox_threshold_hi = ret;

        taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi < taos_datap->prox_thres_hi_max) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_max;
        taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > taos_datap->prox_thres_hi_min) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_min;

        taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;

        taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo < taos_datap->prox_thres_lo_max) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_max;
        taos_cfgp->prox_threshold_lo = (taos_cfgp->prox_threshold_lo > taos_datap->prox_thres_lo_min) ? taos_cfgp->prox_threshold_lo : taos_datap->prox_thres_lo_min;

		SENSOR_LOG_ERROR("prox_th_high  = %d\n",taos_cfgp->prox_threshold_hi);
		SENSOR_LOG_ERROR("prox_th_low   = %d\n",taos_cfgp->prox_threshold_lo);

		input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
		input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
		input_sync(taos_datap->p_idev);
	}

taos_prox_threshold_use_calibrate_finish:
    return err;
}

static void taos_prox_threshold_auto_cal(void)
{
    int prox_sum  = 0;
    int prox_mean = 0;
    int prox_max  = 0;
    int ret       = 0;
    u8  i         = 0;
    u8  j         = 0;

    mdelay(20);
    for (i = 0, j = 0; i < 5; i++)
    {
        if ((ret = taos_prox_poll(&prox_cal_info[i])) < 0)
        {
            j++;
            SENSOR_LOG_ERROR("call to prox_poll failed in ioctl prox_calibrate\n");
        }
        prox_sum += prox_cal_info[i].prox_data;
        if (prox_cal_info[i].prox_data > prox_max)
            prox_max = prox_cal_info[i].prox_data;
        mdelay(20);
    }

    prox_mean = prox_sum/5;
    if (j==0)
    {
        taos_cfgp->prox_threshold_hi = prox_mean + PROX_THRESHOLD_SAFE_DISTANCE;
        taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > PROX_THRESHOLD_HIGH_MIN) ? taos_cfgp->prox_threshold_hi : PROX_THRESHOLD_HIGH_MIN;
        taos_cfgp->prox_threshold_hi = (taos_cfgp->prox_threshold_hi > taos_datap->prox_thres_hi_min) ? taos_cfgp->prox_threshold_hi : taos_datap->prox_thres_hi_min;
        taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;

        if( prox_mean >800 || taos_cfgp->prox_threshold_hi > 1000 || taos_cfgp->prox_threshold_lo > 900)
        {
            taos_cfgp->prox_threshold_hi= 800;
            taos_cfgp->prox_threshold_lo = taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;
        }

        SENSOR_LOG_INFO("prox_threshold_hi = %d\n",taos_cfgp->prox_threshold_hi );
        SENSOR_LOG_INFO("prox_threshold_lo = %d\n",taos_cfgp->prox_threshold_lo );

        input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
        input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
        input_sync(taos_datap->p_idev);
	}
}

static void taos_prox_threshold_use_default(struct taos_cfg *p_taos_cfgp)
{
    taos_datap->prox_thres_type = PROX_THRESHOLD_USE_DEFAULT;
    p_taos_cfgp->prox_threshold_hi = PROX_THRESHOLD_HIGH_MAX;
    p_taos_cfgp->prox_threshold_lo = p_taos_cfgp->prox_threshold_hi - PROX_THRESHOLD_DISTANCE;

    input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
    input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
    input_sync(taos_datap->p_idev);
}

static int taos_prox_on(void)
{
    int  ret = 0;
    u8 reg_cntrl = 0;

    taos_datap->prox_on = 1;
    als_poll_time_mul = 2;
    SENSOR_LOG_INFO("######## PROX ON #########\n");

    if (true==taos_datap->als_on)
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x01), TAOS_ALS_ADC_TIME_WHEN_PROX_ON))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
            goto taos_prox_on_failed;
        }
    }
    else
    {
        if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x01), 0XFF))) < 0)
        {
            SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
            goto taos_prox_on_failed;
        }
    }

    taos_update_sat_als();

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x02), taos_cfgp->prox_adc_time))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x03), taos_cfgp->prox_wait_time))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0C), taos_cfgp->prox_intr_filter))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0D), taos_cfgp->prox_config))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0E), taos_cfgp->prox_pulse_cnt))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

    reg_cntrl = TAOS_TRITON_CNTL_PROX_DET_ENBL | TAOS_TRITON_CNTL_PWRON    | TAOS_TRITON_CNTL_PROX_INT_ENBL |
		                                         TAOS_TRITON_CNTL_ADC_ENBL | TAOS_TRITON_CNTL_WAIT_TMR_ENBL  ;
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), reg_cntrl))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        goto taos_prox_on_failed;
    }

	taos_datap->pro_ft = true;

    if (taos_datap->prox_thres_have_cal==false)
    {
        if (taos_datap->prox_auto_calibrate)
        {
            taos_prox_threshold_auto_cal();
        }
        else
        {
            taos_prox_threshold_use_default(taos_cfgp);
        }
    }

    taos_prox_threshold_set();
    taos_irq_ops(true, true);

    SENSOR_LOG_INFO("Sueecee\n");
    return ret;

taos_prox_on_failed:
    SENSOR_LOG_INFO("Failed\n");
    return ret;
}


static int taos_prox_off(void)
{
    int ret = 0;
    taos_datap->prox_on = 0;

    SENSOR_LOG_INFO("######## PROX OFF #########\n");

    if (taos_datap->prox_offset_cal_start == true)
    {
        goto taos_prox_off_finish;
    }

    if (true == (taos_datap->proximity_wakelock).locked)
    {
    	hrtimer_cancel(&taos_datap->prox_unwakelock_timer);
        taos_wakelock_ops(&(taos_datap->proximity_wakelock), false);
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), 0x00))) < 0)
    {
        SENSOR_LOG_ERROR("i2c_smbus_write_byte_data failed in ioctl als_off\n");
        goto taos_prox_off_error;
    }

    als_poll_time_mul = 1;

	if (true == taos_datap->als_on)
    {
        taos_sensors_als_poll_on();
	}

    if (true == taos_datap->irq_enabled)
    {
        taos_irq_ops(false, true);
    }

taos_prox_off_finish:
    SENSOR_LOG_INFO("Success\n");
    return ret;

taos_prox_off_error:
    SENSOR_LOG_INFO("Failed\n");
    return ret;
}


static int taos_prox_calibrate(void)
{
    int ret;
    int prox_sum = 0, prox_mean = 0, prox_max = 0,prox_min = 1024;
    u8 reg_cntrl = 0;
    u8 reg_val = 0;
    int i = 0, j = 0;

    struct taos_prox_info *prox_cal_info = NULL;
    prox_cal_info = kmalloc(sizeof(struct taos_prox_info) * (taos_datap->prox_calibrate_times), GFP_KERNEL);
    if (NULL == prox_cal_info)
    {
        SENSOR_LOG_ERROR("malloc prox_cal_info failed\n");
        ret = -1;
        goto prox_calibrate_error1;
    }
    memset(prox_cal_info, 0, sizeof(struct taos_prox_info) * (taos_datap->prox_calibrate_times));

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x01), taos_cfgp->prox_int_time))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_int_time reg\n");
        goto prox_calibrate_error;
    }
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x02), taos_cfgp->prox_adc_time))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_adc_time reg\n");
        goto prox_calibrate_error;
    }
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x03), taos_cfgp->prox_wait_time))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_wait_time reg\n");
        goto prox_calibrate_error;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0D), taos_cfgp->prox_config))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_config reg\n");
        goto prox_calibrate_error;
    }

    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0E), taos_cfgp->prox_pulse_cnt))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_pulse_cnt reg\n");
        goto prox_calibrate_error;
    }
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x1E), taos_cfgp->prox_config_offset))) < 0)
    {
        SENSOR_LOG_ERROR(KERN_ERR "i2c_smbus_write_byte_data failed in ioctl prox_on\n");
        return (ret);
    }
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|0x0F), taos_cfgp->prox_gain))) < 0)
    {
        SENSOR_LOG_ERROR("failed write prox_gain reg\n");
        goto prox_calibrate_error;
    }

    reg_cntrl = reg_val | (TAOS_TRITON_CNTL_PROX_DET_ENBL | TAOS_TRITON_CNTL_PWRON | TAOS_TRITON_CNTL_ADC_ENBL);
    if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG | TAOS_TRITON_CNTRL), reg_cntrl))) < 0)
    {
       SENSOR_LOG_ERROR("failed write cntrl reg\n");
       goto prox_calibrate_error;
    }

    prox_sum = 0;
    prox_max = 0;
    prox_min =1024;
    mdelay(30);
    for (i = 0; i < (taos_datap->prox_calibrate_times); i++)
    {
        if ((ret = taos_prox_poll(&prox_cal_info[i])) < 0)
        {
        	j++;
            SENSOR_LOG_ERROR("call to prox_poll failed in ioctl prox_calibrate\n");
        }

        prox_sum += prox_cal_info[i].prox_data;

        if (prox_cal_info[i].prox_data > prox_max)
        {
            prox_max = prox_cal_info[i].prox_data;
        }

	    if (prox_cal_info[i].prox_data < prox_min)
	    {
            prox_min = prox_cal_info[i].prox_data;
        }

        SENSOR_LOG_ERROR("prox get time %d data is %d",i,prox_cal_info[i].prox_data);
        mdelay(30);
    }

    prox_mean = prox_sum/(taos_datap->prox_calibrate_times);
    if(j == 0)
 	{
        taos_cfgp->prox_threshold_hi = ((((prox_max - prox_mean) * prox_calibrate_hi_param) + 50)/100) + prox_mean+120;
        taos_cfgp->prox_threshold_lo = ((((prox_max - prox_mean) * prox_calibrate_lo_param) + 50)/100) + prox_mean+40;
    }

	if( prox_mean >700 || taos_cfgp->prox_threshold_hi > 1000 || taos_cfgp->prox_threshold_lo > 900)
	{
        taos_cfgp->prox_threshold_hi = prox_threshold_hi_param;
        taos_cfgp->prox_threshold_lo = prox_threshold_lo_param;
        prox_config_offset_param=0x0;
        taos_cfgp->prox_config_offset = prox_config_offset_param;
	}

    SENSOR_LOG_ERROR("------------ taos_cfgp->prox_threshold_hi = %d\n",taos_cfgp->prox_threshold_hi );
    SENSOR_LOG_ERROR("------------ taos_cfgp->prox_threshold_lo = %d\n",taos_cfgp->prox_threshold_lo );
    SENSOR_LOG_ERROR("------------ taos_cfgp->prox_mean = %d\n",prox_mean );
    SENSOR_LOG_ERROR("------------ taos_cfgp->prox_max = %d\n",prox_max );
    SENSOR_LOG_ERROR("------------ taos_cfgp->prox_min = %d\n",prox_min );

    for (i = 0; i < sizeof(taos_triton_reg_init); i++)
    {
        if(i !=11)
        {
            if ((ret = (i2c_smbus_write_byte_data(taos_datap->client, (TAOS_TRITON_CMD_REG|(TAOS_TRITON_CNTRL +i)), taos_triton_reg_init[i]))) < 0)
            {
                SENSOR_LOG_ERROR("failed write triton_init reg\n");
                           goto prox_calibrate_error;

            }
         }
     }

    input_report_rel(taos_datap->p_idev, REL_Y, taos_cfgp->prox_threshold_hi);
    input_report_rel(taos_datap->p_idev, REL_Z, taos_cfgp->prox_threshold_lo);
    input_sync(taos_datap->p_idev);
	kfree(prox_cal_info);
	return 1;
prox_calibrate_error:
    SENSOR_LOG_ERROR("exit\n");
	 kfree(prox_cal_info);
prox_calibrate_error1:

	return -1;
}


MODULE_AUTHOR("John Koshi - Surya Software");
MODULE_DESCRIPTION("TAOS ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");

module_init(taos_init);
module_exit(taos_exit);

