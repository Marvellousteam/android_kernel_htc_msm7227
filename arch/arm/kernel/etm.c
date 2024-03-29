/*
 * linux/arch/arm/kernel/etm.c
 *
 * Driver for ARM's Embedded Trace Macrocell and Embedded Trace Buffer.
 *
 * Copyright (C) 2009 Nokia Corporation.
 * Alexander Shishkin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/amba/bus.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <asm/hardware/coresight.h>
#include <asm/sections.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Shishkin");

static struct tracectx tracer;

static inline bool trace_isrunning(struct tracectx *t)
{
	return !!(t->flags & TRACER_RUNNING);
}

static int etm_setup_address_range(struct tracectx *t, int n,
		unsigned long start, unsigned long end, int exclude, int data)
{
	u32 flags = ETMAAT_ARM | ETMAAT_IGNCONTEXTID | ETMAAT_IGNSECURITY |
		    ETMAAT_NOVALCMP;

	if (n < 1 || n > t->ncmppairs)
		return -EINVAL;

	/* comparators and ranges are numbered starting with 1 as opposed
	 * to bits in a word */
	n--;

	if (data)
		flags |= ETMAAT_DLOADSTORE;
	else
		flags |= ETMAAT_IEXEC;

	/* first comparator for the range */
	etm_writel(t, flags, ETMR_COMP_ACC_TYPE(n * 2));
	etm_writel(t, start, ETMR_COMP_VAL(n * 2));

	/* second comparator is right next to it */
	etm_writel(t, flags, ETMR_COMP_ACC_TYPE(n * 2 + 1));
	etm_writel(t, end, ETMR_COMP_VAL(n * 2 + 1));

	flags = exclude ? ETMTE_INCLEXCL : 0;
	etm_writel(t, flags | (1 << n), ETMR_TRACEENCTRL);

	return 0;
}

static int trace_start(struct tracectx *t)
{
	u32 v;
	unsigned long timeout = TRACER_TIMEOUT;

	etb_unlock(t);

	etb_writel(t, 0, ETBR_WRITEADDR);
	etb_writel(t, 0, ETBR_FORMATTERCTRL);
	etb_writel(t, 1, ETBR_CTRL);

	etb_lock(t);

	/* configure etm */
	v = ETMCTRL_OPTS | ETMCTRL_PROGRAM | ETMCTRL_PORTSIZE(t->etm_portsz);

	if (t->flags & TRACER_CYCLE_ACC)
		v |= ETMCTRL_CYCLEACCURATE;

	etm_unlock(t);

	etm_writel(t, v, ETMR_CTRL);

	while (!(etm_readl(t, ETMR_CTRL) & ETMCTRL_PROGRAM) && --timeout)
		;
	if (!timeout) {
		dev_dbg(t->dev, "Waiting for progbit to assert timed out\n");
		etm_lock(t);
		return -EFAULT;
	}

	etm_setup_address_range(t, 1, (unsigned long)_stext,
			(unsigned long)_etext, 0, 0);
	etm_writel(t, 0, ETMR_TRACEENCTRL2);
	etm_writel(t, 0, ETMR_TRACESSCTRL);
	etm_writel(t, 0x6f, ETMR_TRACEENEVT);

	v &= ~ETMCTRL_PROGRAM;
	v |= ETMCTRL_PORTSEL;

	etm_writel(t, v, ETMR_CTRL);

	timeout = TRACER_TIMEOUT;
	while (etm_readl(t, ETMR_CTRL) & ETMCTRL_PROGRAM && --timeout)
		;
	if (!timeout) {
		dev_dbg(t->dev, "Waiting for progbit to deassert timed out\n");
		etm_lock(t);
		return -EFAULT;
	}

	etm_lock(t);

	t->flags |= TRACER_RUNNING;

	return 0;
}

static int trace_stop(struct tracectx *t)
{
	unsigned long timeout = TRACER_TIMEOUT;

	etm_unlock(t);

	etm_writel(t, 0x440, ETMR_CTRL);
	while (!(etm_readl(t, ETMR_CTRL) & ETMCTRL_PROGRAM) && --timeout)
		;
	if (!timeout) {
		dev_dbg(t->dev, "Waiting for progbit to assert timed out\n");
		etm_lock(t);
		return -EFAULT;
	}

	etm_lock(t);

	etb_unlock(t);
	etb_writel(t, ETBFF_MANUAL_FLUSH, ETBR_FORMATTERCTRL);

	timeout = TRACER_TIMEOUT;
	while (etb_readl(t, ETBR_FORMATTERCTRL) &
			ETBFF_MANUAL_FLUSH && --timeout)
		;
	if (!timeout) {
		dev_dbg(t->dev, "Waiting for formatter flush to commence "
				"timed out\n");
		etb_lock(t);
		return -EFAULT;
	}

	etb_writel(t, 0, ETBR_CTRL);

	etb_lock(t);

	t->flags &= ~TRACER_RUNNING;

	return 0;
}

static int etb_getdatalen(struct tracectx *t)
{
	u32 v;
	int wp;

	v = etb_readl(t, ETBR_STATUS);

	if (v & 1)
		return t->etb_bufsz;

	wp = etb_readl(t, ETBR_WRITEADDR);
	return wp;
}

/* sysrq+v will always stop the running trace and leave it at that */
static void etm_dump(void)
{
	struct tracectx *t = &tracer;
	u32 first = 0;
	int length;

	if (!t->etb_regs) {
		printk(KERN_INFO "No tracing hardware found\n");
		return;
	}

	if (trace_isrunning(t))
		trace_stop(t);

	etb_unlock(t);

	length = etb_getdatalen(t);

	if (length == t->etb_bufsz)
		first = etb_readl(t, ETBR_WRITEADDR);

	etb_writel(t, first, ETBR_READADDR);

	printk(KERN_INFO "Trace buffer contents length: %d\n", length);
	printk(KERN_INFO "--- ETB buffer begin ---\n");
	for (; length; length--)
		printk("%08x", cpu_to_be32(etb_readl(t, ETBR_READMEM)));
	printk(KERN_INFO "\n--- ETB buffer end ---\n");

	etb_lock(t);
}

static void sysrq_etm_dump(int key, struct tty_struct *tty)
{
	if (!mutex_trylock(&tracer.mutex)) {
		printk(KERN_INFO "Tracing hardware busy\n");
		return;
	}
	dev_dbg(tracer.dev, "Dumping ETB buffer\n");
	etm_dump();
	mutex_unlock(&tracer.mutex);
}

static struct sysrq_key_op sysrq_etm_op = {
	.handler = sysrq_etm_dump,
	.help_msg = "ETM buffer dump",
	.action_msg = "etm",
};

static int etb_open(struct inode *inode, struct file *file)
{
	if (!tracer.etb_regs)
		return -ENODEV;

	file->private_data = &tracer;

	return nonseekable_open(inode, file);
}

static ssize_t etb_read(struct file *file, char __user *data,
		size_t len, loff_t *ppos)
{
	int total, i;
	long length;
	struct tracectx *t = file->private_data;
	u32 first = 0;
	u32 *buf;
	int wpos;
	int skip;
	long wlength;
	loff_t pos = *ppos;

	mutex_lock(&t->mutex);

	if (trace_isrunning(t)) {
		length = 0;
		goto out;
	}

	etb_unlock(t);

	total = etb_getdatalen(t);
	if (total == t->etb_bufsz)
		first = etb_readl(t, ETBR_WRITEADDR);

	if (pos > total * 4) {
		skip = 0;
		wpos = total;
	} else {
		skip = (int)pos % 4;
		wpos = (int)pos / 4;
	}
	total -= wpos;
	first = (first + wpos) % t->etb_bufsz;

	etb_writel(t, first, ETBR_READADDR);

	wlength = min(total, DIV_ROUND_UP(skip + (int)len, 4));
	length = min(total * 4 - skip, (int)len);
	buf = vmalloc(wlength * 4);

	dev_dbg(t->dev, "ETB read %ld bytes to %lld from %ld words at %d\n",
		length, pos, wlength, first);
	dev_dbg(t->dev, "ETB buffer length: %d\n", total + wpos);
	dev_dbg(t->dev, "ETB status reg: %x\n", etb_readl(t, ETBR_STATUS));
	for (i = 0; i < wlength; i++)
		buf[i] = etb_readl(t, ETBR_READMEM);

	etb_lock(t);

	length -= copy_to_user(data, (u8 *)buf + skip, length);
	vfree(buf);
	*ppos = pos + length;

out:
	mutex_unlock(&t->mutex);

	return length;
}

static int etb_release(struct inode *inode, struct file *file)
{
	/* there's nothing to do here, actually */
	return 0;
}

static const struct file_operations etb_fops = {
	.owner = THIS_MODULE,
	.read = etb_read,
	.open = etb_open,
	.release = etb_release,
};

static struct miscdevice etb_miscdev = {
	.name = "tracebuf",
	.minor = 0,
	.fops = &etb_fops,
};

static int __init etb_probe(struct amba_device *dev, struct amba_id *id)
{
	struct tracectx *t = &tracer;
	int ret = 0;

	ret = amba_request_regions(dev, NULL);
	if (ret)
		goto out;

	mutex_lock(&t->mutex);
	t->etb_regs = ioremap_nocache(dev->res.start, resource_size(&dev->res));
	if (!t->etb_regs) {
		ret = -ENOMEM;
		goto out_release;
	}

	amba_set_drvdata(dev, t);

	etb_unlock(t);
	t->etb_bufsz = etb_readl(t, ETBR_DEPTH);
	dev_dbg(&dev->dev, "Size: %x\n", t->etb_bufsz);

	/* make sure trace capture is disabled */
	etb_writel(t, 0, ETBR_CTRL);
	etb_writel(t, 0x1000, ETBR_FORMATTERCTRL);
	etb_lock(t);
	mutex_unlock(&t->mutex);

	etb_miscdev.parent = &dev->dev;

	ret = misc_register(&etb_miscdev);
	if (ret)
		goto out_unmap;

	/* Get optional clock. Currently used to select clock source on omap3 */
	t->emu_clk = clk_get(&dev->dev, "emu_src_ck");
	if (IS_ERR(t->emu_clk))
		dev_dbg(&dev->dev, "Failed to obtain emu_src_ck.\n");
	else
		clk_enable(t->emu_clk);

	dev_dbg(&dev->dev, "ETB AMBA driver initialized.\n");

out:
	return ret;

out_unmap:
	mutex_lock(&t->mutex);
	amba_set_drvdata(dev, NULL);
	iounmap(t->etb_regs);
	t->etb_regs = NULL;

out_release:
	mutex_unlock(&t->mutex);
	amba_release_regions(dev);

	return ret;
}

static int etb_remove(struct amba_device *dev)
{
	struct tracectx *t = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	iounmap(t->etb_regs);
	t->etb_regs = NULL;

	if (!IS_ERR(t->emu_clk)) {
		clk_disable(t->emu_clk);
		clk_put(t->emu_clk);
	}

	amba_release_regions(dev);

	return 0;
}

static struct amba_id etb_ids[] = {
	{
		.id	= 0x0003b907,
		.mask	= 0x0007ffff,
	},
	{ 0, 0 },
};

static struct amba_driver etb_driver = {
	.drv		= {
		.name	= "etb",
		.owner	= THIS_MODULE,
	},
	.probe		= etb_probe,
	.remove		= etb_remove,
	.id_table	= etb_ids,
};

/* use a sysfs file "trace_running" to start/stop tracing */
static ssize_t trace_running_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sprintf(buf, "%x\n", trace_isrunning(&tracer));
}

static ssize_t trace_running_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t n)
{
	unsigned int value;
	int ret;

	if (sscanf(buf, "%u", &value) != 1)
		return -EINVAL;

	mutex_lock(&tracer.mutex);
	if (!tracer.etb_regs)
		ret = -ENODEV;
	else
		ret = value ? trace_start(&tracer) : trace_stop(&tracer);
	mutex_unlock(&tracer.mutex);

	return ret ? : n;
}

static struct kobj_attribute trace_running_attr =
	__ATTR(trace_running, 0644, trace_running_show, trace_running_store);

static ssize_t trace_info_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	u32 etb_wa, etb_ra, etb_st, etb_fc, etm_ctrl, etm_st;
	int datalen;

	mutex_lock(&tracer.mutex);
	if (tracer.etb_regs) {
		etb_unlock(&tracer);
		datalen = etb_getdatalen(&tracer);
		etb_wa = etb_readl(&tracer, ETBR_WRITEADDR);
		etb_ra = etb_readl(&tracer, ETBR_READADDR);
		etb_st = etb_readl(&tracer, ETBR_STATUS);
		etb_fc = etb_readl(&tracer, ETBR_FORMATTERCTRL);
		etb_lock(&tracer);
	} else {
		etb_wa = etb_ra = etb_st = etb_fc = ~0;
		datalen = -1;
	}

	etm_unlock(&tracer);
	etm_ctrl = etm_readl(&tracer, ETMR_CTRL);
	etm_st = etm_readl(&tracer, ETMR_STATUS);
	etm_lock(&tracer);
	mutex_unlock(&tracer.mutex);

	return sprintf(buf, "Trace buffer len: %d\nComparator pairs: %d\n"
			"ETBR_WRITEADDR:\t%08x\n"
			"ETBR_READADDR:\t%08x\n"
			"ETBR_STATUS:\t%08x\n"
			"ETBR_FORMATTERCTRL:\t%08x\n"
			"ETMR_CTRL:\t%08x\n"
			"ETMR_STATUS:\t%08x\n",
			datalen,
			tracer.ncmppairs,
			etb_wa,
			etb_ra,
			etb_st,
			etb_fc,
			etm_ctrl,
			etm_st
			);
}

static struct kobj_attribute trace_info_attr =
	__ATTR(trace_info, 0444, trace_info_show, NULL);

static ssize_t trace_mode_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	return sprintf(buf, "%d %d\n",
			!!(tracer.flags & TRACER_CYCLE_ACC),
			tracer.etm_portsz);
}

static ssize_t trace_mode_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t n)
{
	unsigned int cycacc, portsz;

	if (sscanf(buf, "%u %u", &cycacc, &portsz) != 2)
		return -EINVAL;

	mutex_lock(&tracer.mutex);
	if (cycacc)
		tracer.flags |= TRACER_CYCLE_ACC;
	else
		tracer.flags &= ~TRACER_CYCLE_ACC;

	tracer.etm_portsz = portsz & 0x0f;
	mutex_unlock(&tracer.mutex);

	return n;
}

static struct kobj_attribute trace_mode_attr =
	__ATTR(trace_mode, 0644, trace_mode_show, trace_mode_store);

static int __init etm_probe(struct amba_device *dev, struct amba_id *id)
{
	struct tracectx *t = &tracer;
	int ret = 0;

	if (t->etm_regs) {
		dev_dbg(&dev->dev, "ETM already initialized\n");
		ret = -EBUSY;
		goto out;
	}

	ret = amba_request_regions(dev, NULL);
	if (ret)
		goto out;

	t->etm_regs = ioremap_nocache(dev->res.start, resource_size(&dev->res));
	if (!t->etm_regs) {
		ret = -ENOMEM;
		goto out_release;
	}

	amba_set_drvdata(dev, t);

	t->dev = &dev->dev;
	t->flags = TRACER_CYCLE_ACC;
	t->etm_portsz = 1;

	etm_unlock(t);
	ret = etm_readl(t, CSCR_PRSR);

	t->ncmppairs = etm_readl(t, ETMR_CONFCODE) & 0xf;
	etm_writel(t, 0x440, ETMR_CTRL);
	etm_lock(t);

	ret = sysfs_create_file(&dev->dev.kobj,
			&trace_running_attr.attr);
	if (ret)
		goto out_unmap;

	/* failing to create any of these two is not fatal */
	ret = sysfs_create_file(&dev->dev.kobj, &trace_info_attr.attr);
	if (ret)
		dev_dbg(&dev->dev, "Failed to create trace_info in sysfs\n");

	ret = sysfs_create_file(&dev->dev.kobj, &trace_mode_attr.attr);
	if (ret)
		dev_dbg(&dev->dev, "Failed to create trace_mode in sysfs\n");

	dev_dbg(t->dev, "ETM AMBA driver initialized.\n");

out:
	return ret;

out_unmap:
	amba_set_drvdata(dev, NULL);
	iounmap(t->etm_regs);

out_release:
	amba_release_regions(dev);

	return ret;
}

static int etm_remove(struct amba_device *dev)
{
	struct tracectx *t = amba_get_drvdata(dev);

	amba_set_drvdata(dev, NULL);

	iounmap(t->etm_regs);
	t->etm_regs = NULL;

	amba_release_regions(dev);

	sysfs_remove_file(&dev->dev.kobj, &trace_running_attr.attr);
	sysfs_remove_file(&dev->dev.kobj, &trace_info_attr.attr);
	sysfs_remove_file(&dev->dev.kobj, &trace_mode_attr.attr);

	return 0;
}

static struct amba_id etm_ids[] = {
	{
		.id	= 0x0003b921,
		.mask	= 0x0007ffff,
	},
	{ 0, 0 },
};

static struct amba_driver etm_driver = {
	.drv		= {
		.name   = "etm",
		.owner  = THIS_MODULE,
	},
	.probe		= etm_probe,
	.remove		= etm_remove,
	.id_table	= etm_ids,
};

static int __init etm_init(void)
{
	int retval;

	mutex_init(&tracer.mutex);

	retval = amba_driver_register(&etb_driver);
	if (retval) {
		printk(KERN_ERR "Failed to register etb\n");
		return retval;
	}

	retval = amba_driver_register(&etm_driver);
	if (retval) {
		amba_driver_unregister(&etb_driver);
		printk(KERN_ERR "Failed to probe etm\n");
		return retval;
	}

	/* not being able to install this handler is not fatal */
	(void)register_sysrq_key('v', &sysrq_etm_op);

	return 0;
}

device_initcall(etm_init);

