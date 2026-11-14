#ifndef NX3DX_LINUX_FILE_PICKER_H
#define NX3DX_LINUX_FILE_PICKER_H

/* Open a Linux GUI picker for a custom soundtrack and copy the chosen file
 * into the save directory path supplied by app_wants_bgm_pick().
 * Returns 1 only when a complete, non-empty file has been saved. */
int linux_pick_bgm(const char *destination);

#endif
