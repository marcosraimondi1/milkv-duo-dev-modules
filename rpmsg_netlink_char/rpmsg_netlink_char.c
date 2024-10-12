// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging module with netlink
 * and a character device
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
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>

#define NETLINK_USER        19
#define RPMSG_ENDPOINT_NAME "kws-app"
#define DRIVER_NAME         "rpmsg_netlink_kws"

#define CLASS_NAME  "rpmsg_class"
#define DEVICE_NAME "rpmsg_char_dev"
#define BUFFER_SIZE 1024

static struct cdev rpmsg_cdev;
static dev_t dev_num;
static char msg_buffer[BUFFER_SIZE];
static int msg_len;
static struct class *rpmsg_class;
static struct device *rpmsg_device;

struct rpmsg_device *rpmsg_dev = NULL;
struct driver_data {
	struct sock *nl_sk;
	int client_pid;
};

/**
 * @brief Leer el mensaje desde el dispositivo de caracter
 * @param filep Puntero al archivo
 * @param buffer Puntero al buffer en el espacio de usuario
 * @param len Tamano del buffer
 * @param offset Puntero al offset
 * @return Numero de bytes leidos
 */
static ssize_t rpmsg_dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
	int ret;

	// Verificar si ya se ley0 el mensaje completo
	if (*offset >= msg_len) {
		return 0; // Fin del archivo
	}

	// Ajustar la longitud si es necesario
	if (len > msg_len - *offset) {
		len = msg_len - *offset;
	}

	// Copiar el mensaje al espacio de usuario
	ret = copy_to_user(buffer, msg_buffer + *offset, len);
	if (ret) {
		pr_err("rpmsg_char_dev: Error al copiar los datos al espacio de usuario\n");
		return -EFAULT;
	}

	// Actualizar el offset
	*offset += len;

	return len;
}

static int rpmsg_dev_open(struct inode *inodep, struct file *filep)
{
	return 0;
}

static int rpmsg_dev_release(struct inode *inodep, struct file *filep)
{
	return 0;
}

// Definir las operaciones del dispositivo
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = rpmsg_dev_open,
	.read = rpmsg_dev_read,
	.release = rpmsg_dev_release,
};

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

	// Guardar el mensaje en el buffer
	msg_len = len;
	if (msg_len > BUFFER_SIZE - 1) {
		msg_len = BUFFER_SIZE - 1;
	}
	memcpy(msg_buffer, data, msg_len);
	msg_buffer[msg_len] = '\0'; // Asegurarse de que termine en null

	// Enviar a userspace por Netlink si hay un usuario conectado
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
	char empty_msg[] = "";

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

	// send first sync message to complete ept creation
	send_rpmsg(rpmsg_dev, empty_msg, sizeof(empty_msg));

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

static int __init rpmsg_netlink_init(void)
{
	int ret;

	pr_info("rpmsg_netlink: Iniciando el módulo\n");

	// Asignar un numero mayor y menor para el dispositivo
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("rpmsg_char_dev: No se pudo asignar el número de dispositivo\n");
		return ret;
	}

	// Crear una clase para el dispositivo
	rpmsg_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(rpmsg_class)) {
		unregister_chrdev_region(dev_num, 1);
		pr_err("rpmsg_char_dev: No se pudo crear la clase\n");
		return PTR_ERR(rpmsg_class);
	}

	// Crear el dispositivo
	rpmsg_device = device_create(rpmsg_class, NULL, dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(rpmsg_device)) {
		class_destroy(rpmsg_class);
		unregister_chrdev_region(dev_num, 1);
		pr_err("rpmsg_char_dev: No se pudo crear el dispositivo\n");
		return PTR_ERR(rpmsg_device);
	}

	// Inicializar y agregar el dispositivo de caracter
	cdev_init(&rpmsg_cdev, &fops);
	ret = cdev_add(&rpmsg_cdev, dev_num, 1);
	if (ret < 0) {
		device_destroy(rpmsg_class, dev_num);
		class_destroy(rpmsg_class);
		unregister_chrdev_region(dev_num, 1);
		pr_err("rpmsg_char_dev: No se pudo agregar el dispositivo\n");
		return ret;
	}

	pr_info("rpmsg_char_dev: Dispositivo registrado correctamente\n");

	return register_rpmsg_driver(&rpmsg_client);
}

/**
 * @brief Salir del modulo
 */
static void __exit rpmsg_netlink_exit(void)
{
	// Eliminar el dispositivo de carácter
	cdev_del(&rpmsg_cdev);
	device_destroy(rpmsg_class, dev_num);
	class_destroy(rpmsg_class);
	unregister_chrdev_region(dev_num, 1);

	unregister_rpmsg_driver(&rpmsg_client);
	pr_info("rpmsg_netlink: Módulo cerrado\n");
}

module_init(rpmsg_netlink_init);
module_exit(rpmsg_netlink_exit);

MODULE_AUTHOR("Marcos Raimondi <marcosraimondi1@gmail.com>");
MODULE_DESCRIPTION("Remote processor messaging module with netlink");
MODULE_LICENSE("GPL v2");
