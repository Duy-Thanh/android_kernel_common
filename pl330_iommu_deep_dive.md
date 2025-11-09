# PL330 DMA 與 IOMMU 連結機制深度解析

本文件詳細記錄了從硬體描述 (Device Tree) 到 Linux 核心內部具體程式碼實現的完整追蹤，闡述了 `pl330` DMA 控制器如何與 IOMMU 建立關聯，並最終啟用 IOMMU 進行地址轉譯的全部流程。

---

### 總覽

整個流程並非由 `pl330` 驅動程式主導，而是由核心的 **IOMMU 子系統**根據 **Device Tree** 中的靜態描述，在系統啟動過程中動態建立的。這是一個從「靜態藍圖」到「動態連結」再到「實際應用」的標準 Linux 驅動模型範例。

---

### 階段一：硬體藍圖 - Device Tree (`.dts`)

這是一切的起點和不可動搖的「證據」。硬體工程師透過 `iommus` 屬性，在 Device Tree 中明確定義了 `pl330` DMA 控制器和 IOMMU 之間的拓撲關係。

**實際範例程式碼 (`.dts`):**
```dts
dma0: dma-controller@3b990000 {
    compatible = "arm,pl330", "arm,primecell";
    reg = <0 0x3b990000 0 0x1000>;
    // ... 其他屬性 ...

    // 關鍵連結指令！
    // 這行程式碼告訴核心：
    // 1. dma0 這個裝置必須受到 inf_smmu 這個 IOMMU 的管理。
    // 2. 它在 IOMMU 中的身分證號碼 (Stream ID) 是 0x8200。
    iommus = <&inf_smmu 0x8200>;
};
```
- **核心作用**: `iommus` 屬性是觸發後續所有自動化設定的**唯一**原因。沒有它，`pl330` 和 IOMMU 在核心看來就是兩個無關的裝置。

---

### 階段二：核心啟動與裝置探測 - 建立軟體連結 (程式碼證據核心)

當 Linux 核心啟動並解析 Device Tree 時，它會根據上述藍圖建立起實際的軟體連結。這個過程的核心是一個「**通知器 (Notifier)**」機制，由 IOMMU 子系統主導。

#### 程式碼追蹤路徑：

1.  **註冊監聽器 (`iommu_bus_notifier`)**
    -   **位置**: `drivers/iommu/iommu.c`
    -   **行為**: 在 IOMMU 子系統初始化時，它會向核心註冊一個名為 `iommu_bus_notifier` 的監聽器。這個監聽器會「訂閱」系統中所有裝置的加入 (`DEVICE_ADD`) 事件。

2.  **`pl330` 裝置加入，觸發回呼**
    -   **行為**: 當核心探測到 `pl330` (`dma0`) 並將其加入系統時，`iommu_bus_notifier` 的回呼函式 `iommu_probe_device()` 會被自動觸發。

3.  **解析 Device Tree (`of_iommu_configure`)**
    -   **位置**: `drivers/iommu/iommu.c` -> `iommu_probe_device()`
    -   **行為**: `iommu_probe_device()` 函式會呼叫 `of_iommu_configure()`。此函式的**唯一目的**就是去檢查傳入裝置 (`pl330`) 的 Device Tree 節點，尋找 `iommus` 屬性。

4.  **建立連結 (`__iommu_device_link`)**
    -   **位置**: `drivers/iommu/of_iommu.c` -> `of_iommu_configure()`
    -   **行為**: `of_iommu_configure()` 成功解析出 `iommus` 屬性後，會找到 `&inf_smmu` 對應的 IOMMU 裝置，並呼叫 `iommu_device_link()` (其核心為 `__iommu_device_link`) 來執行最終的連結動作。

#### 最關鍵的程式碼證據：

**檔案**: `drivers/iommu/iommu.c`
**函式**: `__iommu_device_link()`

```c
static int __iommu_device_link(struct iommu_device *iommu_dev,
                               struct device *dev)
{
    // ...
    dev->iommu = iommu_dev; // <--- 就是這一行！
    // ...
    return 0;
}
```
- **結論**: **`dev->iommu = iommu_dev;`** 這一行程式碼就是 `pl330` 裝置與 IOMMU 在軟體層面被「綁定」的瞬間。它將 `pl330` 裝置結構體中的 `iommu` 指標，指向了 `inf_smmu` 的 IOMMU 物件。

---

### 階段三：設定 DMA 操作 - 應用已建立的連結

在驅動初始化的後期，當系統為 `pl330` 設定 DMA 功能時 (`iommu_setup_dma_ops(dev)` 被呼叫)，之前建立的連結就派上用場了。

**檔案**: `drivers/iommu/dma-iommu.c`
**函式**: `iommu_setup_dma_ops()`

```c
void iommu_setup_dma_ops(struct device *dev)
{
    // 1. 因為 dev->iommu 在階段二已被設定，所以這裡能成功取得 domain
    struct iommu_domain *domain = iommu_get_domain_for_dev(dev);

    // ...

    // 2. 檢查 domain 是否為 DMA 類型
    dev->dma_iommu = iommu_is_dma_domain(domain);

    if (dev->dma_iommu && iommu_dma_init_domain(domain, dev))
        goto out_err;
    
    // ...
}
```
- **行為**:
    1. `iommu_get_domain_for_dev(dev)` 函式內部會檢查 `dev->iommu` 指標。因為指標已經被設定，函式便能成功地從 `inf_smmu` 驅動程式取得一個 DMA 用的 `iommu_domain`。
    2. `iommu_is_dma_domain(domain)` 檢查這個 domain 的類型，回傳 `true`。
    3. `dev->dma_iommu` 被設為 `true`，正式宣告此裝置的所有 DMA 操作都需經由 IOMMU 處理。

---

### 最終總結

整個流程是一個清晰的因果鏈：

- **因**: Device Tree 中的 `iommus` 屬性。
- **過程**: 核心 IOMMU 子系統在裝置探測時，自動解析該屬性並執行 `dev->iommu = iommu_dev;`，完成軟體層面的綁定。
- **果**: 在設定 DMA 操作時，系統根據 `dev->iommu` 的綁定狀態，最終將 `dev->dma_iommu` 旗標設為 `true`，啟用 IOMMU 功能。
