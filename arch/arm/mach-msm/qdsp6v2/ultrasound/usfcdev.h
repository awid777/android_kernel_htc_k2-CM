/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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
#ifndef __USFCDEV_H__
#define __USFCDEV_H__

#include <linux/input.h>

#define TSC_EVENT_TYPE_IND 0
#define MAX_EVENT_TYPE_NUM 1

bool usfcdev_register(
	uint16_t event_type_ind,
	bool (*match_cb)(uint16_t, struct input_dev *dev));
void usfcdev_unregister(uint16_t event_type_ind);
bool usfcdev_set_filter(uint16_t event_type_ind, bool filter);
#endif 
