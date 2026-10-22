/****************************************************************************
 * chips/bk7258/bk7258_drv_model.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Armino device model
 *
 * The 12 symbols of middleware/driver/common/drv_model.c: a table of
 * DD_MAX_DEV slots, each holding a state, a use count and a pointer to the
 * device's DD_OPERATIONS, plus the open/close/read/write/control dispatch
 * that goes through it.
 *
 * The handle is the slot index with a magic word in the top half
 * (DD_HANDLE_MAGIC_WORD 0xA5A50000), so every entry point starts by
 * validating that magic and the index before touching the table.
 *
 * Critical sections are the authority's GLOBAL_INT_DISABLE/RESTORE pair, i.e.
 * rtos_disable_int()/rtos_enable_int() -- the same save-and-restore shape used
 * across the baseline, and the reason the state and use-count updates are not
 * lost between an ISR and a task.
 *
 * ddev_control() carries one extra guard the others do not have: after the
 * handle check it re-tests idx against DD_MAX_DEV.  It is redundant with
 * ddev_get_id_from_handle()'s DD_DEV_TYPE_END bound, but the authority has it
 * and a device whose type sits between DD_DEV_TYPE_END and DD_MAX_DEV would
 * read past the table without it, so it stays.
 *
 * Live symbols here are drv_model_init, ddev_get_id_from_handle and
 * ddev_control.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

#include <os/mem.h>
#include <os/os.h>

#include "bk_drv_model.h"
#include "drv_model.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DRV_DEV_S drv_dev_tbl[DD_MAX_DEV] =
{
  {0},
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

UINT32 drv_model_init(void)
{
  os_memset(drv_dev_tbl, 0, sizeof(drv_dev_tbl));

  return DRV_SUCCESS;
}

UINT32 drv_model_uninit(void)
{
  os_memset(drv_dev_tbl, 0, sizeof(drv_dev_tbl));

  return DRV_SUCCESS;
}

UINT32 ddev_check_handle(DD_HANDLE handle)
{
  UINT32 magic;
  UINT32 idx;

  magic = handle & DD_HANDLE_MAGIC_MASK;
  idx   = handle & DD_HANDLE_ID_MASK;
  if (DD_HANDLE_MAGIC_WORD == magic && idx < DD_MAX_DEV)
    {
      return DRV_SUCCESS;
    }
  else
    {
      return DRV_FAILURE;
    }
}

DD_HANDLE ddev_make_handle(UINT32 idx)
{
  UINT32 handle = DD_HANDLE_UNVALID;

  if (idx >= DD_MAX_DEV)
    {
      goto make_exit;
    }

  handle = idx + DD_HANDLE_MAGIC_WORD;

make_exit:
  return handle;
}

UINT32 ddev_get_id_from_handle(DD_HANDLE handle)
{
  UINT32 magic;
  UINT32 idx;

  magic = handle & DD_HANDLE_MAGIC_MASK;
  idx   = handle & DD_HANDLE_ID_MASK;

  if (magic != DD_HANDLE_MAGIC_WORD || idx >= DD_DEV_TYPE_END)
    {
      return DD_ID_UNVALID;
    }

  return idx;
}

DD_HANDLE ddev_open(dd_device_type dev, UINT32 *status, UINT32 op_flag)
{
  UINT32 idx;
  UINT32 handle;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  GLOBAL_INT_DECLARATION();

  handle = DD_HANDLE_UNVALID;
  idx = ddev_get_id_from_handle(dev);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  *status = DRV_FAILURE;

  dev_ptr = &drv_dev_tbl[idx];
  if (DD_STATE_OPENED == dev_ptr->state)
    {
      handle = ddev_make_handle(idx);
    }
  else if (DD_STATE_CLOSED == dev_ptr->state)
    {
      handle = ddev_make_handle(idx);

      operation = dev_ptr->op;
      if (operation && operation->open)
        {
          *status = (operation->open)(op_flag);
        }

      GLOBAL_INT_DISABLE();
      dev_ptr->state = DD_STATE_OPENED;
      dev_ptr->use_cnt = 0;
      GLOBAL_INT_RESTORE();
    }
  else
    {
    }

  GLOBAL_INT_DISABLE();
  dev_ptr->use_cnt++;
  GLOBAL_INT_RESTORE();

  return handle;
}

UINT32 ddev_close(DD_HANDLE handle)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  GLOBAL_INT_DECLARATION();

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  dev_ptr = &drv_dev_tbl[idx];

  GLOBAL_INT_DISABLE();
  dev_ptr->use_cnt--;
  GLOBAL_INT_RESTORE();

  if (0 == dev_ptr->use_cnt)
    {
      operation = dev_ptr->op;
      if (operation && operation->close)
        {
          (operation->close)();
        }

      GLOBAL_INT_DISABLE();
      dev_ptr->state = DD_STATE_CLOSED;
      GLOBAL_INT_RESTORE();
    }

  return DRV_SUCCESS;
}

UINT32 ddev_read(DD_HANDLE handle, char *user_buf, UINT32 count,
                 UINT32 op_flag)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  status = DRV_FAILURE;
  dev_ptr = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->read)
    {
      status = (operation->read)(user_buf, count, op_flag);
    }

  return status;
}

UINT32 ddev_write(DD_HANDLE handle, char *user_buf, UINT32 count,
                  UINT32 op_flag)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  status    = DRV_FAILURE;
  dev_ptr   = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->write)
    {
      status = (operation->write)(user_buf, count, op_flag);
    }

  return status;
}

UINT32 ddev_control(DD_HANDLE handle, UINT32 cmd, VOID *param)
{
  UINT32 idx;
  UINT32 status;
  DRV_DEV_PTR dev_ptr;
  DD_OPERATIONS *operation;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID == idx)
    {
      return DRV_FAILURE;
    }

  if (idx >= DD_MAX_DEV)
    {
      return DRV_FAILURE;
    }

  status    = DRV_FAILURE;
  dev_ptr   = &drv_dev_tbl[idx];
  operation = dev_ptr->op;
  if (operation && operation->control)
    {
      status = (operation->control)(cmd, param);
    }

  return status;
}

UINT32 ddev_register_dev(DD_HANDLE handle, DD_OPERATIONS *optr)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;

  if (!optr)
    {
      return DRV_FAILURE;
    }

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID != idx)
    {
      dev_ptr = &drv_dev_tbl[idx];
      if (DD_STATE_NODEVICE == dev_ptr->state)
        {
          dev_ptr->op    = optr;
          dev_ptr->state = DD_STATE_CLOSED;
        }
      else
        {
          return DRV_FAILURE;
        }
    }
  else
    {
      return DRV_FAILURE;
    }

  return DRV_SUCCESS;
}

UINT32 ddev_unregister_dev(DD_HANDLE handle)
{
  UINT32 idx;
  DRV_DEV_PTR dev_ptr;

  idx = ddev_get_id_from_handle(handle);
  if (DD_ID_UNVALID != idx)
    {
      dev_ptr = &drv_dev_tbl[idx];
      dev_ptr->op    = 0;
      dev_ptr->state = DD_STATE_NODEVICE;
    }

  return DRV_SUCCESS;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
