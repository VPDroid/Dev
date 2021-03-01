/*
 * linux/container.h: definitions for container module
 * Copyright 2016-2019 xdja Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#ifndef __LINUX_CONTAINER_H__
#define __LINUX_CONTAINER_H__


struct container_info_t {
	/**
     * The drv_ns of the container.
     */
	void *drv_ns;

	/**
     * The index of the container.
     */
	int idx;

    /**
     * The pos of the container.
     */
	int pos;
};


#define CONTAINER_GET_IDX		_IOWR('c', 1, int)
#define CONTAINER_GET_POS		_IOWR('c', 2, int)
#define CONTAINER_GET_INFO		_IOWR('c', 3, struct container_info_t)
#define CONTAINER_SET_INFO		_IOWR('c', 4, struct container_info_t)
#define CONTAINER_GET_MODE		_IOWR('c', 5, int)
#define CONTAINER_GET_ACTIVE_MODE		_IOWR('c', 6, int)


#endif /* __LINUX_CONTAINER_H__ */
