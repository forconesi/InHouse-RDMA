#include <xen/xenbus.h>

struct xenvif {
	/* Unique identifier for this interface. */
	domid_t          domid;
	unsigned int     handle;
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

struct xenvif *xenvif_alloc(struct device *parent, domid_t domid,
			    unsigned int handle)
{
	struct xenvif *vif;

	if ((vif = kzalloc(sizeof(struct xenvif), GFP_KERNEL)) == NULL)
		return NULL;

	vif->domid  = domid;
	vif->handle = handle;

	pr_debug("Successfully created xenvif\n");

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

static void connect(struct backend_info *be)
{
	pr_debug("connect!!\n");
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
}

static void unregister_hotplug_status_watch(struct backend_info *be)
{
	if (be->have_hotplug_status_watch) {
		unregister_xenbus_watch(&be->hotplug_status_watch);
		kfree(be->hotplug_status_watch.node);
	}
	be->have_hotplug_status_watch = 0;
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
#if 0
	return add_uevent_var(env, "vif=%s", be->vif->dev->name);
#else
	return 0;
#endif
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
