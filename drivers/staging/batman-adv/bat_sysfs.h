/*
 * Copyright (C) 2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */


#define SYSFS_IF_MESH_SUBDIR "mesh"
#define SYSFS_IF_BAT_SUBDIR "batman_adv"

int sysfs_add_meshif(struct net_device *dev);
void sysfs_del_meshif(struct net_device *dev);
int sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev);
void sysfs_del_hardif(struct kobject **hardif_obj);