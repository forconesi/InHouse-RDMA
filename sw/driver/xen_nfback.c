#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/interface/io/netif.h>
#include <linux/netdevice.h>	/* IFNAMSIZ */

struct xenvif {
	/* Unique identifier for this interface. */
	domid_t          domid;
	unsigned int     handle;

	/* vif of netback is origianally virtual network device
	 * but, for nfback no need to register a network device */
	char		name[IFNAMSIZ];
	struct device	*parent_dev;

	/* When feature-split-event-channels = 0, tx_irq = rx_irq. */
	unsigned int tx_irq;
	/* Only used when feature-split-event-channels = 1 */
	char tx_irq_name[IFNAMSIZ+4]; /* DEVNAME-tx */
	struct xen_netif_tx_back_ring tx;

	/* When feature-split-event-channels = 0, tx_irq = rx_irq. */
	unsigned int rx_irq;
	/* Only used when feature-split-event-channels = 1 */
	char rx_irq_name[IFNAMSIZ+4]; /* DEVNAME-rx */
	struct xen_netif_rx_back_ring rx;
};

struct backend_info {
	struct xenbus_device *dev;
	struct xenvif *vif;

	/* This is the state that will be reflected in xenstore when any
	 * active hotplug script completes.
	 */
	enum xenbus_state state;

	enum xenbus_state frontend_state;
	struct xenbus_watch hotplug_status_watch;
	u8 have_hotplug_status_watch:1;
};

static irqreturn_t xenvif_tx_interrupt(int irq, void *dev_id)
{       
#if 0
	struct xenvif *vif = dev_id;

	if (RING_HAS_UNCONSUMED_REQUESTS(&vif->tx))
		napi_schedule(&vif->napi);
#endif
	return IRQ_HANDLED;
}

static irqreturn_t xenvif_rx_interrupt(int irq, void *dev_id)
{       
#if 0
	struct xenvif *vif = dev_id;

	xenvif_kick_thread(vif);
#endif
	return IRQ_HANDLED;
}


static irqreturn_t xenvif_interrupt(int irq, void *dev_id)
{
	xenvif_tx_interrupt(irq, dev_id);
	xenvif_rx_interrupt(irq, dev_id);

	return IRQ_HANDLED;
}

struct xenvif *xenvif_alloc(struct device *parent, domid_t domid,
		unsigned int handle)
{
	struct xenvif *vif;

	if ((vif = kzalloc(sizeof(struct xenvif), GFP_KERNEL)) == NULL)
		return NULL;

	vif->domid  = domid;
	vif->handle = handle;
	vif->parent_dev = parent;
	snprintf(vif->name, IFNAMSIZ - 1, "vif%u.%u", domid, handle);

	pr_debug("Successfully created xenvif\n");
#if 0
	__module_get(THIS_MODULE);
#endif

	return vif;
}

static inline void backend_switch_state(struct backend_info *be,
		enum xenbus_state state)
{
	struct xenbus_device *dev = be->dev;

	pr_debug("%s -> %s\n", dev->nodename, xenbus_strstate(state));
	be->state = state;

	/* If we are waiting for a hotplug script then defer the
	 * actual xenbus state change.
	 */
	if (!be->have_hotplug_status_watch)
		xenbus_switch_state(dev, state);
}

static inline struct xenbus_device *xenvif_to_xenbus_device(struct xenvif *vif)
{                        
	return to_xenbus_device(vif->parent_dev);
}

void xenvif_unmap_frontend_rings(struct xenvif *vif)
{
	if (vif->tx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
				vif->tx.sring);
	if (vif->rx.sring)
		xenbus_unmap_ring_vfree(xenvif_to_xenbus_device(vif),
				vif->rx.sring);
}

int xenvif_map_frontend_rings(struct xenvif *vif,
		grant_ref_t tx_ring_ref,
		grant_ref_t rx_ring_ref)
{       
	void *addr;
	struct xen_netif_tx_sring *txs;
	struct xen_netif_rx_sring *rxs;

	int err = -ENOMEM;

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(vif),
			tx_ring_ref, &addr);
	if (err)
		goto err;

	txs = (struct xen_netif_tx_sring *)addr;
	BACK_RING_INIT(&vif->tx, txs, PAGE_SIZE);

	err = xenbus_map_ring_valloc(xenvif_to_xenbus_device(vif),
			rx_ring_ref, &addr);
	if (err)         
		goto err;

	rxs = (struct xen_netif_rx_sring *)addr;
	BACK_RING_INIT(&vif->rx, rxs, PAGE_SIZE);

	return 0;

err:    
	xenvif_unmap_frontend_rings(vif);
	return err;
}

int xenvif_connect(struct xenvif *vif, unsigned long tx_ring_ref,
		unsigned long rx_ring_ref, unsigned int tx_evtchn,
		unsigned int rx_evtchn)
{
	int err = -ENOMEM;

	BUG_ON(vif->tx_irq);

	err = xenvif_map_frontend_rings(vif, tx_ring_ref, rx_ring_ref);
	if (err < 0)
		goto err;

	if (tx_evtchn == rx_evtchn) {
		/* feature-split-event-channels == 0 */
		err = bind_interdomain_evtchn_to_irqhandler(
				vif->domid, tx_evtchn, xenvif_interrupt, 0,
				vif->name, vif);
		if (err < 0)
			goto err_unmap;
		vif->tx_irq = vif->rx_irq = err;
		disable_irq(vif->tx_irq);
	} else {
		/* feature-split-event-channels == 1 */
		snprintf(vif->tx_irq_name, sizeof(vif->tx_irq_name),
				"%s-tx", vif->name);
		err = bind_interdomain_evtchn_to_irqhandler(
				vif->domid, tx_evtchn, xenvif_tx_interrupt, 0,
				vif->tx_irq_name, vif);
		if (err < 0)
			goto err_unmap;
		vif->tx_irq = err;
		disable_irq(vif->tx_irq);

		snprintf(vif->rx_irq_name, sizeof(vif->rx_irq_name),
				"%s-rx", vif->name);
		err = bind_interdomain_evtchn_to_irqhandler(
				vif->domid, rx_evtchn, xenvif_rx_interrupt, 0,
				vif->rx_irq_name, vif);
		if (err < 0)
			goto err_tx_unbind;
		vif->rx_irq = err;
		disable_irq(vif->rx_irq);
	}

	return 0;

#if 0
err_rx_unbind:
	unbind_from_irqhandler(vif->rx_irq, vif);
	vif->rx_irq = 0;
#endif
err_tx_unbind:
	unbind_from_irqhandler(vif->tx_irq, vif);
	vif->tx_irq = 0;
err_unmap:
	xenvif_unmap_frontend_rings(vif);
err:
#if 0
	module_put(THIS_MODULE);
#endif
	return err;
}

static int connect_rings(struct backend_info *be)
{
	struct xenvif *vif = be->vif;
	struct xenbus_device *dev = be->dev;
	unsigned long tx_ring_ref, rx_ring_ref;
	unsigned int tx_evtchn, rx_evtchn, rx_copy;
	int err;

	err = xenbus_gather(XBT_NIL, dev->otherend,
			"tx-ring-ref", "%lu", &tx_ring_ref,
			"rx-ring-ref", "%lu", &rx_ring_ref, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err,
				"reading %s/ring-ref",
				dev->otherend);
		return err;
	}

	/* Try split event channels first, then single event channel. */
	err = xenbus_gather(XBT_NIL, dev->otherend,
			"event-channel-tx", "%u", &tx_evtchn,
			"event-channel-rx", "%u", &rx_evtchn, NULL);
	if (err < 0) {
		err = xenbus_scanf(XBT_NIL, dev->otherend,
				"event-channel", "%u", &tx_evtchn);
		if (err < 0) {
			xenbus_dev_fatal(dev, err,
					"reading %s/event-channel(-tx/rx)",
					dev->otherend);
			return err;
		}
		rx_evtchn = tx_evtchn;
	}

	err = xenbus_scanf(XBT_NIL, dev->otherend, "request-rx-copy", "%u",
			&rx_copy);
	if (err == -ENOENT) {
		err = 0;
		rx_copy = 0;
	}
	if (err < 0) {
		xenbus_dev_fatal(dev, err, "reading %s/request-rx-copy",
				dev->otherend);
		return err;
	}
	if (!rx_copy)
		return -EOPNOTSUPP;

	/* Map the shared frame, irq etc. */
	err = xenvif_connect(vif, tx_ring_ref, rx_ring_ref,
			tx_evtchn, rx_evtchn);
	if (err) {
		xenbus_dev_fatal(dev, err,
				"mapping shared-frames %lu/%lu port tx %u rx %u",
				tx_ring_ref, rx_ring_ref,
				tx_evtchn, rx_evtchn);
		return err;
	}
	return 0;
}

static void unregister_hotplug_status_watch(struct backend_info *be)
{
	if (be->have_hotplug_status_watch) {
		unregister_xenbus_watch(&be->hotplug_status_watch);
		kfree(be->hotplug_status_watch.node);
	}
	be->have_hotplug_status_watch = 0;
}

static void hotplug_status_changed(struct xenbus_watch *watch,
		const char **vec,
		unsigned int vec_size)
{
	struct backend_info *be = container_of(watch,
			struct backend_info,
			hotplug_status_watch);
	char *str;
	unsigned int len;

	str = xenbus_read(XBT_NIL, be->dev->nodename, "hotplug-status", &len);
	if (IS_ERR(str))
		return;
	if (len == sizeof("connected")-1 && !memcmp(str, "connected", len)) {
		/* Complete any pending state change */
		xenbus_switch_state(be->dev, be->state);

		/* Not interested in this watch anymore. */
		unregister_hotplug_status_watch(be);
	}
	kfree(str);
}

static void connect(struct backend_info *be)
{
	int err;
	struct xenbus_device *dev = be->dev;

	pr_debug("connect!!\n");

	err = connect_rings(be);
	if (err)
		return;
	pr_debug("connect_rings succeeded!\n");

	unregister_hotplug_status_watch(be);
	err = xenbus_watch_pathfmt(dev, &be->hotplug_status_watch,
			hotplug_status_changed,
			"%s/%s", dev->nodename, "hotplug-status");
	if (!err)
		be->have_hotplug_status_watch = 1;

}

static void backend_create_xenvif(struct backend_info *be)
{
	int err;
	long handle;
	struct xenbus_device *dev = be->dev;

	if (be->vif != NULL)
		return;

	err = xenbus_scanf(XBT_NIL, dev->nodename, "handle", "%li", &handle);
	if (err != 1) {
		xenbus_dev_fatal(dev, err, "reading handle");
		return;
	}

	be->vif = xenvif_alloc(&dev->dev, dev->otherend_id, handle);
	if (IS_ERR(be->vif)) {
		err = PTR_ERR(be->vif);
		be->vif = NULL;
		xenbus_dev_fatal(dev, err, "creating interface");
		return;
	}

	kobject_uevent(&dev->dev.kobj, KOBJ_ONLINE);
}

void xenvif_disconnect(struct xenvif *vif)
{
	pr_debug("disconnect!!\n");

	if (vif->tx_irq) {
		if (vif->tx_irq == vif->rx_irq)
			unbind_from_irqhandler(vif->tx_irq, vif);
		else {
			unbind_from_irqhandler(vif->tx_irq, vif);
			unbind_from_irqhandler(vif->rx_irq, vif);
		}
		vif->tx_irq = 0;
	}

	xenvif_unmap_frontend_rings(vif);
}

static void backend_disconnect(struct backend_info *be)
{
	if (be->vif)
		xenvif_disconnect(be->vif);
}

static void backend_connect(struct backend_info *be)
{
	if (be->vif)
		connect(be);
}

void xenvif_free(struct xenvif *vif)
{
	kfree(vif);
#if 0
	module_put(THIS_MODULE);
#endif
}

/* Handle backend state transitions:
 *
 * The backend state starts in InitWait and the following transitions are
 * allowed.
 *
 * InitWait -> Connected
 *
 *    ^    \         |
 *    |     \        |
 *    |      \       |
 *    |       \      |
 *    |        \     |
 *    |         \    |
 *    |          V   V
 *
 *  Closed  <-> Closing
 *
 * The state argument specifies the eventual state of the backend and the
 * function transitions to that state via the shortest path.
 */
static void set_backend_state(struct backend_info *be,
		enum xenbus_state state)
{
	while (be->state != state) {
		switch (be->state) {
			case XenbusStateClosed:
				switch (state) {
					case XenbusStateInitWait:
					case XenbusStateConnected:
						pr_info("%s: prepare for reconnect\n",
								be->dev->nodename);
						backend_switch_state(be, XenbusStateInitWait);
						break;
					case XenbusStateClosing:
						backend_switch_state(be, XenbusStateClosing);
						break;
					default:
						BUG();
				}
				break;
			case XenbusStateInitWait:
				switch (state) {
					case XenbusStateConnected:
						backend_connect(be);
						backend_switch_state(be, XenbusStateConnected);
						break;
					case XenbusStateClosing:
					case XenbusStateClosed:
						backend_switch_state(be, XenbusStateClosing);
						break;
					default:
						BUG();
				}
				break;
			case XenbusStateConnected:
				switch (state) {
					case XenbusStateInitWait:
					case XenbusStateClosing:
					case XenbusStateClosed:
						backend_disconnect(be);
						backend_switch_state(be, XenbusStateClosing);
						break;
					default:
						BUG();
				}
				break;
			case XenbusStateClosing:
				switch (state) {
					case XenbusStateInitWait:
					case XenbusStateConnected:
					case XenbusStateClosed:
						backend_switch_state(be, XenbusStateClosed);
						break;
					default:
						BUG();
				}
				break;
			default:
				BUG();
		}
	}
}

/**
 * Callback received when the frontend's state changes.
 */
static void frontend_changed(struct xenbus_device *dev,
		enum xenbus_state frontend_state)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	pr_debug("%s -> %s\n", dev->otherend, xenbus_strstate(frontend_state));

	be->frontend_state = frontend_state;

	switch (frontend_state) {
		case XenbusStateInitialising:
			set_backend_state(be, XenbusStateInitWait);
			break;

		case XenbusStateInitialised:
			break;

		case XenbusStateConnected:
			set_backend_state(be, XenbusStateConnected);
			break;

		case XenbusStateClosing:
			set_backend_state(be, XenbusStateClosing);
			break;

		case XenbusStateClosed:
			set_backend_state(be, XenbusStateClosed);
			if (xenbus_dev_is_online(dev))
				break;
			/* fall through if not online */
		case XenbusStateUnknown:
			set_backend_state(be, XenbusStateClosed);
			device_unregister(&dev->dev);
			break;

		default:
			xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
					frontend_state);
			break;
	}
}

/* xen support is not mandatory for nf native driver */
static bool registered;

static int nfback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev_get_drvdata(&dev->dev);

	set_backend_state(be, XenbusStateClosed);

	unregister_hotplug_status_watch(be);
	if (be->vif) {
		kobject_uevent(&dev->dev.kobj, KOBJ_OFFLINE);
		xenbus_rm(XBT_NIL, dev->nodename, "hotplug-status");
		xenvif_free(be->vif);
		be->vif = NULL;
	}
	kfree(be);
	dev_set_drvdata(&dev->dev, NULL);
	return 0;
}

static int nfback_probe(struct xenbus_device *dev,
		const struct xenbus_device_id *id)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;
	struct backend_info *be = kzalloc(sizeof(struct backend_info),
			GFP_KERNEL);
	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				"allocating backend structure");
		return -ENOMEM;
	}

	pr_debug("probe!!\n");

	be->dev = dev;
	dev_set_drvdata(&dev->dev, be);

	do {
		err = xenbus_transaction_start(&xbt);
		if (err) {
			xenbus_dev_fatal(dev, err, "starting transaction");
			goto fail;
		}
		err = xenbus_printf(xbt, dev->nodename,
				"feature-rx-copy", "%d", 1);
		if (err) {
			message = "writing feature-rx-copy";
			goto abort_transaction;
		}
		err = xenbus_transaction_end(xbt, 0);
	} while (err == -EAGAIN);

	if (err) {
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto fail;
	}

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	be->state = XenbusStateInitWait;

	/* This kicks hotplug scripts, so do it immediately. */
	backend_create_xenvif(be);

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);
fail:
	pr_debug("failed\n");
	nfback_remove(dev);
	return err;
}

static int nfback_uevent(struct xenbus_device *xdev,
		struct kobj_uevent_env *env)
{
	struct backend_info *be = dev_get_drvdata(&xdev->dev);
	char *val;

	val = xenbus_read(XBT_NIL, xdev->nodename, "script", NULL);
	if (IS_ERR(val)) {
		int err = PTR_ERR(val);
		xenbus_dev_fatal(xdev, err, "reading script");
		return err;
	} else {
		if (add_uevent_var(env, "script=%s", val)) {
			kfree(val);
			return -ENOMEM;
		}
		kfree(val);
	}

	if (!be || !be->vif)
		return 0;

	return add_uevent_var(env, "vif=%s", be->vif->name);
}

static const struct xenbus_device_id nfback_ids[] = {
	{ "vif" },
	{ "" }
};

static DEFINE_XENBUS_DRIVER(nfback, ,
		.probe = nfback_probe,
		.remove = nfback_remove,
		.uevent = nfback_uevent,
		.otherend_changed = frontend_changed,
		);

int xen_nfback_init(void)
{
	return !(registered = xenbus_register_backend(&nfback_driver) == 0);
}

void xen_nfback_fini(void)
{
	if (registered)
		xenbus_unregister_driver(&nfback_driver);
}
