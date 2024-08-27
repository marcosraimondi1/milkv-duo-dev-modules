// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging - sample client driver
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/ktime.h>

#define RPMSG_ENDPOINT_NAME "rpmsg-client-sample"
#define MESSAGE_SIZE        4080
#define NUM_MESSAGES        2300

static char msg[MESSAGE_SIZE];
static ktime_t start_time = 0;

struct instance_data {
	int rx_count;
};

static int rpmsg_sample_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	int ret;
	struct instance_data *idata = dev_get_drvdata(&rpdev->dev);

	// check received data
	if (MESSAGE_SIZE != len || memcmp(data, msg, MESSAGE_SIZE)) {
		dev_err(&rpdev->dev, "data integrity check failed\n");
		pr_err("data: %s\n", (char *)data);
		pr_err("expected %d bytes, received %d bytes\n", MESSAGE_SIZE, len);
		return -EINVAL;
	}

	++idata->rx_count;

	/* samples should not live forever */
	if (idata->rx_count >= NUM_MESSAGES) {
		ktime_t end_time = ktime_get();
		s64 elapsed = ktime_to_ns(ktime_sub(end_time, start_time));
		printk("\n--------- TEST RESULTS ---------------\n");
		printk("messages: %d\n", NUM_MESSAGES);
		printk("message size: %d\n", MESSAGE_SIZE);
		printk("elapsed time: %lld ms\n", elapsed / 1000000);

		dev_info(&rpdev->dev, "goodbye!\n");
		return 0;
	}

	/* send a new message now */
	ret = rpmsg_send(rpdev->ept, msg, MESSAGE_SIZE);
	if (ret) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
	}

	return 0;
}

static int rpmsg_sample_probe(struct rpmsg_device *rpdev)
{
	int i, ret;
	struct instance_data *idata;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	printk("rpmsg mtu is %ld\n", rpmsg_get_mtu(rpdev->ept));

	idata = devm_kzalloc(&rpdev->dev, sizeof(*idata), GFP_KERNEL);
	if (!idata) {
		return -ENOMEM;
	}

	dev_set_drvdata(&rpdev->dev, idata);

	start_time = ktime_get();

	printk("starting speed test\n");
	/* prepare the message */
	for (i = 0; i < MESSAGE_SIZE; i++) {
		msg[i] = 'c';
	}
	msg[MESSAGE_SIZE - 1] = '\0'; /* null-terminate the message */

	/* send a message to our remote processor */
	ret = rpmsg_send(rpdev->ept, msg, MESSAGE_SIZE);
	if (ret) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void rpmsg_sample_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg sample client driver is removed\n");
}

static struct rpmsg_device_id rpmsg_driver_sample_id_table[] = {
	{.name = RPMSG_ENDPOINT_NAME},
	{},
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_sample_id_table);

static struct rpmsg_driver rpmsg_sample_client = {
	.drv.name = KBUILD_MODNAME,
	.id_table = rpmsg_driver_sample_id_table,
	.probe = rpmsg_sample_probe,
	.callback = rpmsg_sample_cb,
	.remove = rpmsg_sample_remove,
};
module_rpmsg_driver(rpmsg_sample_client);

MODULE_DESCRIPTION("Remote processor messaging sample client driver");
MODULE_LICENSE("GPL v2");
