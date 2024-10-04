// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging module with netlink
 *
 * Marcos Raimondi <marcosraimondi1@gmail.com>
 */

#include "linux/device.h"
#include <linux/printk.h>
#include <net/sock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rpmsg.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#define NETLINK_USER        17
#define RPMSG_ENDPOINT_NAME "rpmsg-tflite"
#define DRIVER_NAME         "rpmsg_netlink_tflite"

struct rpmsg_device *rpmsg_dev = NULL;
struct driver_data {
	struct sock *nl_sk;
	int client_pid;
};

static int msg_cnt = 0;

/**
 * @brief Send a message to userspace
 * @param msg Message to send
 * @param msg_size Size of the message
 * @param pid Process ID of the user
 */
static void send_msg_to_userspace(char *msg, int msg_size, struct sock *nl_sock, int pid)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int res;

	// create reply
	skb_out = nlmsg_new(msg_size, 0);
	if (!skb_out) {
		pr_err("rpmsg_netlink: Failed to allocate new skb\n");
		return;
	}

	// put received message into reply
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
	memcpy(nlmsg_data(nlh), msg, msg_size);

	pr_debug("rpmsg_netlink: Sending user %s\n", msg);

	res = nlmsg_unicast(nl_sock, skb_out, pid);
	if (res < 0) {
		pr_err("rpmsg_netlink: Error while sending skb to user\n");
	}
}

/**
 * @brief Send a message to the remote processor
 * @param rpdev Remote processor device
 * @param msg Message to send
 * @param len Size of the message
 */
static void send_rpmsg(struct rpmsg_device *rpdev, char *msg, int len)
{
	int ret;
	long int mtu = rpmsg_get_mtu(rpdev->ept);

	pr_debug("rpmsg_netlink: Sending %d bytes to remote (mtu=%ld)\n", len, mtu);

	if (len > mtu) {
		pr_err("rpmsg_netlink: Message too long\n");
		return;
	}

	ret = rpmsg_send(rpdev->ept, msg, len);

	if (ret) {
		pr_err("rpmsg_netlink: rpmsg_send failed: %d\n", ret);
		return;
	}
}

/**
 * @brief Callback for netlink messages received from userspace
 * @param skb Socket buffer
 */
static void netlink_recv_cb(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	int msg_size;
	char *msg;

	struct driver_data *data = dev_get_drvdata(&rpmsg_dev->dev);

	nlh = (struct nlmsghdr *)skb->data;
	data->client_pid = nlh->nlmsg_pid; /* pid of sending process */
	msg = (char *)nlmsg_data(nlh);
	msg_size = nlmsg_len(nlh);

	pr_debug("rpmsg_netlink: Received from pid %d: %s\n", data->client_pid, msg);

	if (rpmsg_dev) {
		msg_cnt++;
		send_rpmsg(rpmsg_dev, msg, msg_size);
	}
}

/**
 * @brief Callback for rpmsg messages received from remote processor
 * @param rpdev Remote processor device
 * @param data Data received
 * @param len Size of the data
 * @param priv Private data
 * @param src Source of the message
 * @return 0
 */
static int rpmsg_recv_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	struct driver_data *drv_data;

	pr_debug("rpmsg_netlink: (src: 0x%x) %s\n", src, (char *)data);

	drv_data = dev_get_drvdata(&rpdev->dev);
	if (drv_data->client_pid > 0) {
		send_msg_to_userspace(data, len, drv_data->nl_sk, drv_data->client_pid);
	} else {
		pr_err("rpmsg_netlink: No user connected\n");
	}

	return 0;
}

struct netlink_kernel_cfg cfg = {
	.input = netlink_recv_cb,
};

/**
 * @brief Probe function for the rpmsg device
 * @param rpdev Remote processor device
 * @return 0
 */
static int rpmsg_netlink_probe(struct rpmsg_device *rpdev)
{
	struct driver_data *data;

	// save rpmsg device
	rpmsg_dev = rpdev;
	pr_info("rpmsg_netlink: New channel (src) 0x%x -> (dst) 0x%x\n", rpdev->src, rpdev->dst);

	pr_info("rpmsg_netlink: mtu %ld\n", rpmsg_get_mtu(rpdev->ept));

	// allocate memory for the device data
	data = devm_kzalloc(&rpdev->dev, sizeof(struct driver_data), GFP_KERNEL);
	if (!data) {
		pr_err("rpmsg_netlink: Error allocating memory.\n");
		return -ENOMEM;
	}

	// create netlink socket
	data->nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
	if (!data->nl_sk) {
		pr_err("rpmsg_netlink: Error creating socket.\n");
		return -10;
	}

	// save netlink socket
	dev_set_drvdata(&rpdev->dev, data);

	msg_cnt = 0;

	return 0;
}

/**
 * @brief Remove function for the rpmsg device
 * @param rpdev Remote processor device
 */
static void rpmsg_netlink_remove(struct rpmsg_device *rpdev)
{
	struct driver_data *drv_data = dev_get_drvdata(&rpdev->dev);
	netlink_kernel_release(drv_data->nl_sk);
}

static struct rpmsg_device_id rpmsg_driver_id_table[] = {
	{.name = RPMSG_ENDPOINT_NAME},
	{},
};

MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_id_table);

static struct rpmsg_driver rpmsg_client = {
	.drv.name = DRIVER_NAME,
	.drv.owner = THIS_MODULE,
	.id_table = rpmsg_driver_id_table,
	.callback = rpmsg_recv_cb,
	.probe = rpmsg_netlink_probe,
	.remove = rpmsg_netlink_remove,
};

/**
 * @brief Initialize the module
 * @return int
 */
static int __init rpmsg_netlink_init(void)
{
	pr_info("rpmsg_netlink: ept=%s netlink_id=%d\n", RPMSG_ENDPOINT_NAME, NETLINK_USER);
	return register_rpmsg_driver(&rpmsg_client);
}

/**
 * @brief Exit the module
 */
static void __exit rpmsg_netlink_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_client);
	pr_info("rpmsg_netlink: Exited module\n");
}

module_init(rpmsg_netlink_init);
module_exit(rpmsg_netlink_exit);

MODULE_AUTHOR("Marcos Raimondi <marcosraimondi1@gmail.com>");
MODULE_DESCRIPTION("Remote processor messaging module with netlink");
MODULE_LICENSE("GPL v2");
