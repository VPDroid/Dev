/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <cutils/container.h>
#include <cutils/container_dev.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* guoweibin add veth begin*/
#define FISSION_IP_PREFIX   30
#define FISSION_VETH_ADDR_MASK  "255.255.255.252"

typedef struct {
    char name[20];            /* interface name */
    char host_name[2][40];    /* veth peer host name */
    char ip[2][20];           /* cell1 and cell2 ip addr */
    char gate[2][20];         /* cell1 and cell2 gateway */
    int prefix;               /* ip prefix length */
}fission_interface_info;

#define BIT(n)  (1 << (n))
#define CHECK_RET_VALUE(ret, n) ((ret == 0) ? 0 : BIT(n))
#define CHECK_NET_ID(netId, ret) do { \
    if ((netId < RMNET_DATA0) || (netId >= FISSION_INTERFACE_NUM)) \
        return ret; \
}while(0)

#define CHECK_SYS_ID(sysId, ret) do { \
    if ((sysId != 0) && (sysId != 1)) \
        return ret; \
}while(0)

const fission_interface_info fission_interface[FISSION_INTERFACE_NUM] = {
    /* name            hostname1        hostname2         ip1              ip2               gate1            gate2             prefix   */
    {"rmnet_data0", {"cell2_rmnet0", "cell1_rmnet0"}, {"172.17.202.6",  "172.17.202.10"}, {"172.17.202.5",  "172.17.202.9" }, FISSION_IP_PREFIX},
    {"rmnet_data1", {"cell2_rmnet1", "cell1_rmnet1"}, {"172.17.202.14", "172.17.202.18"}, {"172.17.202.13", "172.17.202.17"}, FISSION_IP_PREFIX},
    {"wlan0",       {"cell2_wlan0",  "cell1_wlan0"},  {"172.17.202.22", "172.17.202.26"}, {"172.17.202.21", "172.17.202.25"}, FISSION_IP_PREFIX}
};

const int fission_rtable[FISSION_INTERFACE_NUM] = {
    100, 101, 102
};

const int fission_rt_prio[FISSION_INTERFACE_NUM] = {
    9000, 9001, 9002
};

const char *getInterfaceAddrMask(const int netIdx, const int sysId)
{
    CHECK_NET_ID(netIdx, NULL);
    CHECK_SYS_ID(sysId, NULL);
    return FISSION_VETH_ADDR_MASK;
}

const char *getInterfaceGate(const int netIdx, const int sysId)
{
    CHECK_NET_ID(netIdx, NULL);
    CHECK_SYS_ID(sysId, NULL);
    return fission_interface[netIdx].gate[sysId];
}

const char *getInterfaceHostName(const int netIdx, const int sysId)
{
    CHECK_NET_ID(netIdx, NULL);
    CHECK_SYS_ID(sysId, NULL);
    return fission_interface[netIdx].host_name[sysId];
}

const char *getInterfaceName(const int netIdx)
{
    CHECK_NET_ID(netIdx, NULL);
    return fission_interface[netIdx].name;
}

int getInterfacePrefix(const int netIdx)
{
    CHECK_NET_ID(netIdx, -1);
    return fission_interface[netIdx].prefix;
}

const char *getInterfaceIp(const int netIdx, const int sysId)
{
    CHECK_NET_ID(netIdx, NULL);
    CHECK_SYS_ID(sysId, NULL);
    return fission_interface[netIdx].ip[sysId];
}
int getInterfaceRouteTable(const int netIdx)
{
    CHECK_NET_ID(netIdx, -1);
    return fission_rtable[netIdx];
}

int getInterfaceRulePrio(const int netIdx)
{
    CHECK_NET_ID(netIdx, -1);
    return fission_rt_prio[netIdx];
}


int getInterfaceIdx(const char *ifname)
{
    int i;

    for (i = 0; i < FISSION_INTERFACE_NUM; i++) {
        if (!strcmp(fission_interface[i].name, ifname))
            return i;
    }
    return -1;
}

int configHostVeth(const int netIdx, const int sysId, int isUp)
{
    char buf[256];
    CHECK_NET_ID(netIdx, -1);
    CHECK_SYS_ID(sysId, -1);

    if (isUp)
        snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s up",
            getInterfaceHostName(netIdx, sysId),
            getInterfaceGate(netIdx, sysId),
            getInterfaceAddrMask(netIdx, sysId));
    else
        snprintf(buf, sizeof(buf), "ifconfig %s 0.0.0.0 down",
            getInterfaceHostName(netIdx, sysId));
    return system(buf);
}

int isHostSystem(void)
{
    int mode;
    get_container_mode(&mode);
    return (mode == SYSTEM_TYPE_HOST);
}

int get_container_mode(int *pmode)
{
    int fd;
    int ret;

    fd = open("/dev/container", O_RDWR, 0);
    if (fd < 0)
        return -1;

    ret = ioctl(fd, CONTAINER_GET_MODE, pmode);
    if (ret != 0) {
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}
