#ifndef _UFS_CTMWB_H_
#define _UFS_CTMWB_H_

enum ufs_ctmwb_state {
       UFS_WB_OFF_STATE	= 0,    /* turbo write disabled state */
       UFS_WB_ON_STATE	= 1,            /* turbo write enabled state */
       UFS_WB_ERR_STATE	= 2,            /* turbo write error state */
};

#define ufshcd_is_ctmwb_off(hba) ((hba).ufs_ctmwb_state == UFS_WB_OFF_STATE)
#define ufshcd_is_ctmwb_on(hba) ((hba).ufs_ctmwb_state == UFS_WB_ON_STATE)
#define ufshcd_is_ctmwb_err(hba) ((hba).ufs_ctmwb_state == UFS_WB_ERR_STATE)
#define ufshcd_set_ctmwb_off(hba) ((hba).ufs_ctmwb_state = UFS_WB_OFF_STATE)
#define ufshcd_set_ctmwb_on(hba) ((hba).ufs_ctmwb_state = UFS_WB_ON_STATE)
#define ufshcd_set_ctmwb_err(hba) ((hba).ufs_ctmwb_state = UFS_WB_ERR_STATE)

#define UFS_WB_MANUAL_FLUSH_THRESHOLD	5

struct ufshba_ctmwb {
	enum ufs_ctmwb_state ufs_ctmwb_state;

	bool support_ctmwb_lu;
};

struct ufs_wb_ops *ufshcd_ctmwb_init(void);
#endif
