/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _ADRENO_A4XX_H_
#define _ADRENO_A4XX_H_

int a4xx_perfcounter_enable_vbif(struct adreno_device *adreno_dev,
			unsigned int counter,
			unsigned int countable);

void a4xx_perfcounter_disable_vbif(struct adreno_device *adreno_dev,
			unsigned int counter);

uint64_t a4xx_perfcounter_read_vbif(struct adreno_device *adreno_dev,
			unsigned int counter);

int a4xx_perfcounter_enable_vbif_pwr(struct adreno_device *adreno_dev,
			unsigned int counter);

void a4xx_perfcounter_disable_vbif_pwr(struct adreno_device *adreno_dev,
			unsigned int counter);

uint64_t a4xx_perfcounter_read_vbif_pwr(struct adreno_device *adreno_dev,
			unsigned int counter);

uint64_t a4xx_alwayson_counter_read(struct adreno_device *adreno_dev);

void a4xx_snapshot(struct adreno_device *adreno_dev,
		struct kgsl_snapshot *snapshot);

void a4xx_rbbm_debug_bus_read(struct kgsl_device *device,
	unsigned int block_id, unsigned int index, unsigned int *val);

#endif
