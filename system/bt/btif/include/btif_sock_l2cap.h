/*******************************************************************************
 *  L2CAP Socket Interface
 *******************************************************************************/

#ifndef BTIF_SOCK_L2CAP_H
#define BTIF_SOCK_L2CAP_H



#define L2CAP_MASK_FIXED_CHANNEL    0x10000


bt_status_t btsock_l2cap_init(int handle);
bt_status_t btsock_l2cap_cleanup();
bt_status_t btsock_l2cap_listen(const char* name, int channel,
                              int* sock_fd, int flags);
bt_status_t btsock_l2cap_connect(const bt_bdaddr_t *bd_addr,
                               int channel, int* sock_fd, int flags);
void btsock_l2cap_signaled(int fd, int flags, uint32_t user_id);
void on_l2cap_psm_assigned(int id, int psm);

#endif

