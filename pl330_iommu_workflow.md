# PL330 DMA and IOMMU Linkage Workflow

This document details the complete workflow, from the hardware description (Device Tree) to the kernel source code, of how a `pl330` DMA controller is recognized and configured by the system to use an IOMMU for memory address translation.

---

### Overview

The `pl330` driver itself is not aware of the IOMMU's existence. Its linkage to the IOMMU is dynamically established during kernel boot and device probing, driven by the kernel's core IOMMU subsystem based on settings in the **Device Tree**. The entire process can be divided into three main phases:

1.  **Blueprint Phase**: Defining the master-slave relationship between the `pl330` and the IOMMU in the Device Tree.
2.  **Linkage Phase**: During kernel boot, the IOMMU subsystem parses the Device Tree and establishes a software pointer link between the device (`struct device`) and the IOMMU.
3.  **Application Phase**: When setting up DMA operations for the `pl330`, the system utilizes the established link to direct DMA operations through the IOMMU.

---

### Phase 1: The Hardware Blueprint - Device Tree (`.dts`)

This is the starting point and the most critical piece of evidence. Hardware engineers use the `iommus` property in the Device Tree file to explicitly declare which IOMMU controller should manage the `pl330` DMA controller.

```dts
// / {
//     ...
    // 1. First, define the IOMMU controller (e.g., an ARM SMMU)
    smmu: iommu@f8000000 {
        compatible = "arm,smmu-v2";
        #iommu-cells = <1>;
        // ... other IOMMU-related properties
    };

    // 2. Next, define the PL330 DMA controller
    pl330_dma: dma-controller@f8003000 {
        compatible = "arm,pl330", "arm,primecell";
        // ... other pl330-related properties

        // 3. The Critical Link!
        // This property tells the kernel:
        // "Memory accesses for this pl330_dma device
        // MUST be translated through the 'smmu' IOMMU."
        iommus = <&smmu 0x400>; // '&smmu' is a handle to the IOMMU node
    };
//     ...
// };
```

- **Core Evidence**: The line `iommus = <&smmu ...>;` is the definitive "paper trail" that defines the hardware topology relationship between the two.

---

### Phase 2: Kernel Boot and Device Probing - Creating the Software Link (Concrete Evidence)

When the kernel boots and parses the Device Tree, it uses the blueprint above to create the actual software link. This process is automated.

1.  **Device Added to System**: When the kernel parses the `pl330_dma` node and creates its `struct device` object, it calls `device_add()` to register it with the system.

2.  **IOMMU Subsystem Listens**: During its initialization, the IOMMU subsystem registers a notifier ([`iommu_bus_notifier`](https://elixir.bootlin.com/linux/latest/source/drivers/iommu/iommu.c#L2540)) via `bus_register_notifier()`. This listener reacts to system-wide bus events, specifically `DEVICE_ADD`.

3.  **Probing Device's IOMMU Config**: When the `pl330` device is added, the `iommu_bus_notifier` is triggered, which in turn calls [`iommu_probe_device(dev)`](https://elixir.bootlin.com/linux/latest/source/drivers/iommu/iommu.c#L2498).

4.  **Parsing the Device Tree**: Inside `iommu_probe_device()`, it calls [`of_iommu_configure(dev)`](https://elixir.bootlin.com/linux/latest/source/drivers/iommu/of_iommu.c#L139). This function's purpose is to read the `dev`'s corresponding Device Tree node and look for the `iommus` property.

5.  **Link Found and Pointer Set**: Upon successfully finding the `iommus` property, `of_iommu_configure()` identifies the corresponding IOMMU device (`smmu`) and calls a function to link them, which ultimately executes this critical assignment in [`__iommu_device_link`](https://elixir.bootlin.com/linux/latest/source/drivers/iommu/iommu.c#L2103):

    ```c
    // From: drivers/iommu/iommu.c
    dev->iommu = iommu_dev;
    ```
    **This is the most direct code evidence**: The `dev->iommu` pointer of the `pl330` device is set to point to the `smmu`'s IOMMU object. From this moment, the `pl330` is "bound" to the IOMMU at the software level.

---

### Phase 3: Setting up DMA Operations - Applying the Link

Later in the device driver's initialization, when the system needs to configure DMA capabilities for the `pl330`, our original function of interest, [`iommu_setup_dma_ops(dev)`](https://elixir.bootlin.com/linux/latest/source/drivers/iommu/dma-iommu.c#L2062), is called.

1.  **Get the Domain**: The first line of the function is `struct iommu_domain *domain = iommu_get_domain_for_dev(dev);`.
    -   The logic inside [`iommu_get_domain_for_dev()`](https://elixir.bootlin.com/linux/latest/source/include/linux/iommu.h#L1122) checks the `dev->iommu` pointer.
    -   Because this pointer was set in **Phase 2**, this call successfully requests a dedicated DMA `iommu_domain` from the `smmu` driver.

2.  **Check Domain Type**: Next, the line `dev->dma_iommu = iommu_is_dma_domain(domain);` is executed.
    -   Since the domain obtained in the previous step is specifically for DMA, its `type` property includes the `__IOMMU_DOMAIN_DMA_API` flag.
    -   Therefore, [`iommu_is_dma_domain()`](https://elixir.bootlin.com/linux/latest/source/include/linux/iommu.h#L529) returns `true`.

3.  **Final Configuration**: The `dev->dma_iommu` flag is set to `true`.

### Conclusion

The entire flow clearly demonstrates a process from "static description" to "dynamic linkage" to "practical application":

-   **Cause**: The **Device Tree (`iommus` property)**.
-   **Process**: The **core IOMMU subsystem setting the `dev->iommu` pointer** during device probe, which translates the "cause" into a "software state".
-   **Effect**: The function **`iommu_setup_dma_ops` setting `dev->dma_iommu` based on the state of `dev->iommu`**.

Your inference was correct, and the code paths above provide the definitive evidence for this workflow.
