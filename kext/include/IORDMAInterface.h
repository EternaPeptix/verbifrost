#ifndef IORDMA_INTERFACE_H
#define IORDMA_INTERFACE_H

/*
 * IORDMAInterface.h — The IOKit provider interface for RDMA devices.
 *
 * This is the C++ abstract class exported by IORDMAFamily.kext.
 * RDMA provider kexts subclass IORDMAInterface to register with
 * the RDMA core subsystem.
 *
 * The class hierarchy (confirmed from kernelcache symbol table):
 *   IORDMAInterface : public IOService
 *
 * Provider kexts create a subclass, call registerIBInterface(ib_dev),
 * setNodeGUID(), and registerService(). IORDMAFamily then creates an
 * IORDMAFamilyUC UserClient that libibverbs.dylib discovers.
 */

#include <IOKit/IOService.h>
#include "rdma/ib_verbs.h"

class IORDMAInterface : public IOService
{
    OSDeclareAbstractStructors(IORDMAInterface);

public:
    /* Standard IOKit lifecycle — provider must implement */
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free() override;
    virtual void quiesce();
    virtual IOReturn setProperties(OSObject *properties) override;

    /* RDMA registration — provider calls these in start() */

    /* Register the ib_device with the RDMA core.
     * Called after ib_alloc_device + ib_register_device succeeds. */
    virtual void registerIBInterface(struct ib_device *dev);

    /* Set the node GUID for this device (displayed in ibv_devinfo) */
    virtual void setNodeGUID(uint64_t guid);

    /* Get the ib_device back */
    virtual struct ib_device *getIBDevice();

    /* Get the provider instance */
    virtual void *getInstance();

    /* IOKit user client creation (called when libibverbs opens the device) */
    virtual IOReturn newUserClient(task_t owningTask, void *securityID,
                                   UInt32 type, IOUserClient **handler) override;

    /* Access control */
    virtual bool handleOpen(IOService *forClient, IOOptionBits options,
                            void *arg) override;
    virtual void handleClose(IOService *forClient, IOOptionBits options) override;
    virtual bool handleIsOpen(const IOService *forClient) const override;

    /* Reserved virtual slots (11, for future expansion by Apple) */
    virtual void _RESERVEDIORDMAInterface0();
    virtual void _RESERVEDIORDMAInterface1();
    virtual void _RESERVEDIORDMAInterface2();
    virtual void _RESERVEDIORDMAInterface3();
    virtual void _RESERVEDIORDMAInterface4();
    virtual void _RESERVEDIORDMAInterface5();
    virtual void _RESERVEDIORDMAInterface6();
    virtual void _RESERVEDIORDMAInterface7();
    virtual void _RESERVEDIORDMAInterface8();
    virtual void _RESERVEDIORDMAInterface9();
    virtual void _RESERVEDIORDMAInterface10();
};

#endif /* IORDMA_INTERFACE_H */
